#include "window.hpp"
#include "preferences_display.hpp"
#include "toolbar_tools_display.hpp"
#include "layers_control.hpp"

#include "gonstruct_config.h"
#include <iostream>
#include <memory>

// So we can use Gtk::Stock::DELETE. gtkglext seems to define it (?)
#undef DELETE

using namespace Graal;

namespace {
  bool confirm_changes_level(Graal::level_editor::window& parent,
                      level_editor::level_display& display,
                      int level_x, int level_y) {
    parent.set_current_level(display, level_x, level_y);
    const boost::filesystem::path path = display.get_level_source()->get_level_name(level_x, level_y);

    Glib::ustring basename;
    if (path.empty())
      basename = "new";
    else
      basename = path.filename().string();
    Gtk::MessageDialog security_dialog(
      parent, "The file '" + basename + "' is not saved.", false,
      Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    security_dialog.set_secondary_text(
      "Do you want to save it before closing?");
    security_dialog.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
    security_dialog.add_button(Gtk::Stock::DISCARD, Gtk::RESPONSE_NO);
    security_dialog.add_button(Gtk::Stock::SAVE, Gtk::RESPONSE_YES);

    switch (security_dialog.run()) {
      case Gtk::RESPONSE_CANCEL:
      case Gtk::RESPONSE_DELETE_EVENT:
        return false;
      case Gtk::RESPONSE_NO:
        return true;
      case Gtk::RESPONSE_YES:
        break;
      default:
        throw std::runtime_error("unexpected dialog response");
        break;
    }

    return parent.save_current_page();
  }

  bool confirm_changes(Graal::level_editor::window& parent,
                       level_editor::level_display& display) {
    level_editor::level_display::unsaved_level_map_type::iterator iter, end = display.get_unsaved_levels().end();
    for (iter = display.get_unsaved_levels().begin(); iter != end; ++iter) {
      // Is unsaved
      if (iter->second) {
        int level_x = iter->first.first;
        int level_y = iter->first.second;
        if (!confirm_changes_level(parent, display, level_x, level_y)) {
          return false;
        }
      }
    }

    return true;
  }
}

level_editor::window::tab_label::tab_label(const Glib::ustring& label) {
  Gtk::Image* image = Gtk::manage(new Gtk::Image(Gtk::Stock::CLOSE, Gtk::ICON_SIZE_MENU));
  int width, height;
  Gtk::IconSize::lookup(Gtk::ICON_SIZE_MENU, width, height);

  m_button.set_size_request(width + 4, height + 4);
  m_button.add(*image);
  m_button.set_relief(Gtk::RELIEF_NONE);

  pack_start(m_unsaved);
  pack_start(m_label);
  pack_start(m_button);

  set_label(label);

  show_all();
}

void level_editor::window::tab_label::set_label(const Glib::ustring& label) {
  if (label.empty())
    m_label.set_text("new");
  else
    m_label.set_text(label);
}

void level_editor::window::tab_label::set_unsaved_status(bool status) {
  m_unsaved.set_text(status ? "* " : "");
}

level_editor::window::tab_label::signal_proxy_close level_editor::window::tab_label::close_event() {
  return m_button.signal_clicked();
}

// window
level_editor::window::window(preferences& _prefs)
: fs(_prefs),
  prefs_display(_prefs),
  opening_levels(false),
  m_file_commands(*this, m_header, _prefs),
  m_edit_commands(*this, m_header),
  m_level_commands(*this, m_header, _prefs),
  m_image_cache(fs), display_tileset(_prefs, m_image_cache),
  m_preferences(_prefs),
  m_tile_objects(_prefs),
  m_fc_save(*this, "Save level as", Gtk::FILE_CHOOSER_ACTION_SAVE)
{
  set_title(std::string("Gonstruct ") + GONSTRUCT_VERSION);

  Gtk::HPaned* hpane = Gtk::manage(new Gtk::HPaned());
  Gtk::VBox* vbox_tools = Gtk::manage(new Gtk::VBox());
  Gtk::VBox* vbox_main = Gtk::manage(new Gtk::VBox());
  vbox_main->pack_start(m_header, Gtk::PACK_SHRINK);

  add_accel_group(m_header.get_accel_group());

  // toolbar + edit area + layers control

  // TODO: need to create layers_control here because it uses this in the constructor
  m_layers_control = Gtk::manage(new layers_control(*this, m_preferences));

  // hpane with level + side panel
  m_nb_levels.signal_switch_page().connect_notify(
      sigc::mem_fun(this, &window::on_switch_page), true); // after is important
  m_nb_levels.set_scrollable(true);

  hpane->pack1(m_nb_levels);
  vbox_tools->pack_start(*m_layers_control, Gtk::PACK_SHRINK);
  vbox_tools->pack_start(m_nb_toolset);
  hpane->pack2(*vbox_tools, Gtk::SHRINK);
  hpane->set_position(550);
  vbox_main->pack_start(*hpane);

  // toolset
  // tileset
  Gtk::ScrolledWindow& tileset_scrolled = *Gtk::manage(new Gtk::ScrolledWindow());
  Gdk::Color color;
  color.set_rgb_p(1, 0.6, 0.9);
  display_tileset.modify_bg(Gtk::STATE_NORMAL, color);
  tileset_scrolled.add(display_tileset);
  tileset_scrolled.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  m_nb_toolset.append_page(tileset_scrolled, "Tileset");
  // tools TODO: need to construct it here because it requires the level_display switch signal
  m_tools = Gtk::manage(new toolbar_tools_display(*this, _prefs));
  m_nb_toolset.append_page(*m_tools, "Tools");
  // tile objects
  m_nb_toolset.append_page(m_tile_objects, "Tile Objects");
  //m_status_bar.pack_start(m_status, Gtk::PACK_SHRINK);
  m_status_bar.push("");
  m_status_bar.pack_start(default_tile, Gtk::PACK_SHRINK);
  vbox_main->pack_start(m_status_bar, Gtk::PACK_SHRINK);

  set_default_size(800, 600);

  add(*vbox_main);
  show_all_children();

  prefs_display.signal_preferences_changed().connect(
      sigc::mem_fun(this, &window::on_preferences_changed));

  // connect to tileset_display's signals
  display_tileset.signal_status_update().connect(
      sigc::mem_fun(this, &window::set_status));
  display_tileset.signal_default_tile_changed().connect(
      sigc::mem_fun(this, &window::set_default_tile));
  display_tileset.signal_tiles_selected().connect(
      sigc::mem_fun(this, &window::tiles_selected));
  display_tileset.signal_tileset_updated().connect(
      sigc::mem_fun(this, &window::on_tileset_update));
  /* We need this because Gtk improperly draws over the opengl surface without
   * invalidating it and causing an expose event to fix it */
  display_tileset.signal_expose_event().connect_notify(
      sigc::mem_fun(this, &window::on_tileset_expose_event));

  m_tile_objects.signal_tiles_selected().connect(
      sigc::mem_fun(this, &window::tiles_selected));
  m_tile_objects.signal_create_tile_object().connect(
      sigc::mem_fun(this, &window::get_current_tile_selection));

  // Connect header actions
  m_header.action_help_about->signal_activate().connect(
    sigc::mem_fun(*this, &window::on_action_about));

  std::cout << "Caching...";
  update_cache();
  std::cout << " done" << std::endl;

  set_default_tile(m_preferences.default_tile);
  set_level_buttons(false);

  // FileChooserDialog save as
  Gtk::FileFilter nw_filter;
  nw_filter.add_pattern("*.nw");
  nw_filter.set_name("Graal Level (*.nw)");

  m_fc_save.add_button(Gtk::Stock::CANCEL, Gtk::RESPONSE_CANCEL);
  m_fc_save.add_button("Save", Gtk::RESPONSE_OK);

  m_fc_save.add_filter(nw_filter);
  m_fc_save.set_filter(nw_filter);
  m_fc_save.set_do_overwrite_confirmation(true);
  m_fc_save.set_current_folder(m_preferences.graal_dir);

  // TODO: no
  m_tile_width = 16;
  m_tile_height = 16;

  default_tile.set_tile_size(m_tile_width, m_tile_height);
  display_tileset.set_tile_size(m_tile_width, m_tile_height);
  m_tile_objects.set_tile_size(m_tile_width, m_tile_height);

  m_tile_objects.get();
}

level_editor::window::~window() {
  /* TODO: -1 is (usually) the transparent tile, should probably identify
   * "remember default tile" differently */
  if (m_preferences.default_tile != -1)
    m_preferences.default_tile = default_tile.get_tile();
}

void level_editor::window::on_tileset_update(const Cairo::RefPtr<Cairo::ImageSurface>& surface) {
  m_tile_objects.set_tileset_surface(surface);
  default_tile.set_tileset_surface(surface);
  if (m_nb_levels.get_n_pages() > 0) {
    get_current_level_display()->set_tileset_surface(surface);
  }
}

void level_editor::window::set_level_buttons(bool enabled) {
  m_header.group_level_actions->set_sensitive(enabled);
  m_tile_objects.set_sensitive(enabled);
  m_tools->set_sensitive(enabled);
  m_layers_control->set_sensitive(enabled);
}

void level_editor::window::set_default_tile(int tile_index) {
  // TODO: same as above
  if (tile_index == -1)
    tile_index = 511;
  default_tile.set_tile(tile_index);

  for (int i = 0; i < m_nb_levels.get_n_pages(); i ++) {
    level_display& disp(*get_nth_level_display(i));
    disp.set_default_tile(tile_index);
  }

  level_display* display = get_current_level_display();
  if (display)
    display->clear_selection();
}

void level_editor::window::display_error(const Glib::ustring& message) {
  Gtk::MessageDialog dialog(message, false,
    Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  dialog.set_message("Error");
  dialog.set_secondary_text(message);
  dialog.run();
}

// change tileset to the level's one when the page changes
void level_editor::window::on_switch_page(GtkNotebookPage* page, guint page_num) {
  level_display* display = get_nth_level_display(page_num);

  if (display) {
    if (!opening_levels) {
      display_tileset.update_tileset(display->get_current_level_path().filename().string());
    }

    m_signal_switch_level_display(*display);
  }
}

void level_editor::window::update_cache() {
  if (m_preferences.use_graal_cache) {
    if (!fs.update_cache_from_graal())
      display_error("Told to use Graal file cache, but can't open FILENAMECACHE.txt");
    else
      return;
  }

  fs.update_cache();
}

void level_editor::window::on_preferences_changed(
    level_editor::preferences_display::preference_changes c) {
  if (c & preferences_display::GRAAL_DIR_CHANGED ||
      c & preferences_display::USE_GRAAL_CACHE_CHANGED) {
    m_image_cache.clear_cache();

    update_cache();

    level_display* display(get_current_level_display());
    if (display) {
      display_tileset.update_tileset(display->get_current_level_path().filename().string());
      m_tile_objects.get();
    }
  }

  if (c & preferences_display::REMEMBER_DEFAULT_TILE_CHANGED) {
    set_default_tile(m_preferences.default_tile);
  }
}

std::auto_ptr<level_editor::level_display>
level_editor::window::create_level_display() {
  std::auto_ptr<level_display> display(new level_display(
      m_preferences, m_image_cache,
      default_tile.get_tile()));
  display->set_tile_size(m_tile_width, m_tile_height);

  display->signal_default_tile_changed().connect(
      sigc::mem_fun(this, &window::set_default_tile));
  display->signal_status_update().connect(
      sigc::mem_fun(this, &window::set_status));

  return display;
}

void level_editor::window::tiles_selected(tile_buf& selection, int x, int y) {
  get_current_level_display()->drag_selection(selection, x, y);
}

const boost::shared_ptr<level>& level_editor::window::get_current_level() {
  return get_current_level_display()->get_current_level();
}

bool level_editor::window::close_all_levels() {
  for (int i = 0; i < m_nb_levels.get_n_pages(); i ++) {
    level_display& display(*get_nth_level_display(i));
    if (confirm_changes(*this, display)) {
      m_nb_levels.remove_page(i);
      i --;
    } else {
      return true;
    }
  }

  return false;
}

bool level_editor::window::on_delete_event(GdkEventAny* event) {
  return close_all_levels();
}

void level_editor::window::on_action_about() {
  Gtk::AboutDialog dialog;
  dialog.set_name("Gonstruct");
  dialog.set_version(GONSTRUCT_VERSION);
  dialog.set_comments("An alternative Graal Online level editor");

  dialog.set_copyright("Copyright \302\251 2008 "
                       "Eike Siewertsen, Benjamin Herr\nGraal Online is Copyright \302\251 by Cyberjoueurs"); // TODO

  std::deque<Glib::ustring> authors;
  authors.push_back("Developers:");
  authors.push_back("  Eike Siewertsen <e.siew@londeroth.org>");
  authors.push_back("  Benjamin Herr <ben@0x539.de>");

  std::deque<Glib::ustring> artists;
  artists.push_back("Benjamin Herr <ben@0x539.de>");

  dialog.set_authors(authors);
  dialog.set_artists(artists);

  //dialog.set_logo(m_logo);

  dialog.run();
}

void level_editor::window::load_level(const boost::filesystem::path& file_path, bool activate) {
  // check whether the level is already loaded
  for (int i = 0; i < m_nb_levels.get_n_pages(); i ++) {
    /* Loop through the entire level source, checking whether the level we're
     * trying to load is the same as an already loaded one */
    level_display& display = *get_nth_level_display(i);
    level_map_source& source = *display.get_level_source();
    int width = source.get_width();
    int height = source.get_height();
    for (int x = 0; x < width; x++) {
      for (int y = 0; y < height; y++) {
        boost::filesystem::path level_path;
        // Retrieve level path and compare it
        if (fs.get_path(source.get_level_name(x, y), level_path)) {
          if (file_path == level_path) {
            // Then scroll to the level if we should and return
            if (activate)
              set_current_level(display, x, y);
            return;
          }
        }
      }
    }
  }

  boost::filesystem::path ext = file_path.extension();
  try {
    std::auto_ptr<level_display> display(create_level_display());

    // Load nw level or gmap depending on extension
    if (ext == ".nw") {
      display->load_level(file_path);
    } else if (ext == ".gmap") {
      display->load_gmap(fs, file_path);
    } else {
      throw std::runtime_error("Unknown level extension, can't load " + file_path.filename().string());
    }
    create_new_page(*Gtk::manage(display.release()), activate);
    set_level_buttons(true);
  } catch (const std::exception& e) {
    display_error(e.what());
  }
}

void level_editor::window::create_new_page(level_display& display, bool activate) {
  Gtk::ScrolledWindow* scrolled = Gtk::manage(new Gtk::ScrolledWindow());
  scrolled->add(display);

  // Initialize tab label with the name of the current level
  tab_label* label = Gtk::manage(new tab_label(display.get_current_level_path().filename().string()));
  // Connect tab label's close level signal
  label->close_event().connect(sigc::bind(
    sigc::mem_fun(this, &level_editor::window::on_close_level_clicked),
    sigc::ref(*scrolled), sigc::ref(display)
  ));
  // Update tab label when the title of the display changes
  display.signal_title_changed().connect(
      sigc::mem_fun(*label, &tab_label::set_label));
  display.signal_unsaved_status_changed().connect(
      sigc::mem_fun(*label, &tab_label::set_unsaved_status));

  // Add page to notebook
  m_nb_levels.append_page(*scrolled, *label);
  m_nb_levels.set_tab_reorderable(*scrolled, true);
  scrolled->show_all();
  // Only activate the page if requested
  if (activate)
    m_nb_levels.set_current_page(m_nb_levels.get_n_pages() - 1);
}

void level_editor::window::set_current_level(level_display& display, int level_x, int level_y) {
  for (int i = 0; i < m_nb_levels.get_n_pages(); i ++) {
    if (get_nth_level_display(i) == &display) {
      m_nb_levels.set_current_page(i);
      display.focus_level(level_x, level_y);
      return;
    }
  }

  throw std::runtime_error("set_current_page: no such page currently open");
}

void level_editor::window::on_close_level_clicked(Gtk::ScrolledWindow& scrolled, level_display& display) {
  if (!confirm_changes(*this, display))
    return;

  m_nb_levels.remove(scrolled);

  if (m_nb_levels.get_n_pages() <= 0) {
    set_level_buttons(false);

    m_level_commands.hide_lists();
  }
}

level_editor::level_display* level_editor::window::get_nth_level_display(int n) {
  // TODO: oh god
  Gtk::ScrolledWindow* scrolled =
    static_cast<Gtk::ScrolledWindow*>(m_nb_levels.get_nth_page(n));

  if (!scrolled)
    return 0;

  return static_cast<level_display*>(scrolled->get_child());
}

level_editor::level_display* level_editor::window::get_current_level_display() {
  return get_nth_level_display(m_nb_levels.get_current_page());
}

// Update the tileset of all level displays that start with prefix
void level_editor::window::update_matching_level_displays(const std::string& prefix) {
  level_display* display = get_current_level_display();
  std::string level_name = display->get_current_level_path().filename().string();
  if (level_name.find(prefix) == 0) {
    display_tileset.update_tileset(level_name);
  }
}

Cairo::RefPtr<Cairo::ImageSurface> level_editor::window::get_image(const std::string& file_name) {
  Cairo::RefPtr<Cairo::ImageSurface> image;
  //std::cout << "window: loading image " << file_name << std::endl;

  try {
    image = m_image_cache.get_image(file_name);
  } catch (Cairo::logic_error&) {
    display_error("Filesystem error while trying to load " + file_name);
  } catch (std::bad_alloc& e) {
    display_error("Memory allocation error while trying to load `"
                  + file_name + "': " + e.what());
  } catch (std::exception& e) {
    display_error("Some generic error happened while trying to load `"
                  + file_name + "': " + e.what());
  }

  return image;
}


void level_editor::window::set_status(const std::string& text) {
  m_status_bar.pop();
  m_status_bar.push(text);
}

tile_buf level_editor::window::get_current_tile_selection() {
  level_display& disp = *get_current_level_display();
  tile_buf buf(disp.selection);
  if (buf.empty()) {
    disp.lift_selection();
    buf = disp.selection;
    disp.undo();
    return buf;
  }

  return tile_buf();
}

level_editor::window::signal_switch_level_display_type& level_editor::window::signal_switch_level_display() {
  return m_signal_switch_level_display;
}

void level_editor::window::on_tileset_expose_event(GdkEventExpose* event) {
  level_display* display = get_current_level_display();
  if (display)
    display->invalidate();
}

bool level_editor::window::save_current_page_as() {
  level_display* disp = get_current_level_display();
  boost::filesystem::path path = disp->get_current_level_path();
  if (path.empty())
    m_fc_save.set_current_name("new.nw");
  else
    m_fc_save.set_filename(path.string());


  if (m_fc_save.run() == Gtk::RESPONSE_OK) {
    std::string path = m_fc_save.get_filename();
    level_display* disp = get_current_level_display();
    disp->save_current_level(path);
    m_fc_save.hide();
    return true;
  } else {
    m_fc_save.hide();
    return false;
  }
}

// Return true if everything went fine, false to abort
bool level_editor::window::save_current_page() {
  level_display* disp = get_current_level_display();
  boost::filesystem::path path = disp->get_current_level_path();
  if (path.empty()) {
    return save_current_page_as();
  }

  disp->save_current_level();
  return true;
}
