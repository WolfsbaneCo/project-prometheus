#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "audio.hpp"
#include "backup.hpp"
#include "cloudflare.hpp"
#include "core.hpp"
#include "input.hpp"
#include "library.hpp"
#include "gift_station.hpp"
#include "link.hpp"
#include "netplay.hpp"
#include "settings.hpp"
#include "ui.hpp"
#include "video.hpp"

namespace p64 {

enum class Preset { Clean, Sharp, Retro, Native, Count };

struct Options {
  std::filesystem::path rom;         // start this game straight away
  std::filesystem::path core;        // emulator core library (default: next to the app)
  std::filesystem::path screenshot;  // test mode: write a PNG after `frames` frames and quit
  int frames = 0;
  int width = 1280, height = 720;
  bool fullscreen = false;
  std::string fill, preset;          // test overrides
  std::vector<std::string> keys;     // test mode: "frame:key name" presses
  std::vector<std::string> holds;    // test mode: "from-to:player:button" held N64 buttons
  std::vector<std::string> clicks;   // test mode: "frame:x,y" left clicks, in pixels
  std::vector<std::string> core_options;  // test mode: "key=value" core option overrides
  int poke_frame = 0;                // test mode: write a pattern into save memory at this frame
  std::string poke_text;             // test mode: that pattern, 8 characters (default "PROMETHE")
  std::filesystem::path dump_save;   // test mode: write the core's save memory here on exit
  bool ram_hash = false;             // test mode: print a hash of the console's RAM on exit
  bool online_host = false;          // test mode: host `rom` online at start (prints the invite code)
  std::string online_join;           // test mode: join this invite code at start
  bool link_host = false;            // test mode: play `rom` (GBA) and host a link (prints its code)
  std::string link_join;             // test mode: play `rom` (GBA) and join this link code
  bool online_start = false;         // test mode: the host starts as soon as everyone is ready
  uint32_t online_exit_frame = 0;    // test mode: print the RAM hash at this online frame and quit
  std::string setup_token;           // test mode: run the online setup with this key at start
  std::filesystem::path save_key;    // test mode: save the group's key file here at start
  bool backup_connect = false;       // test mode: connect Google Drive at start (to a stand-in for Google)
  bool backup_sync = false;          // test mode: back up once connected; the app waits for it on exit
};

class App {
 public:
  explicit App(Options options);
  int run();

 private:
  enum class Screen { Home, Playing };
  enum class Overlay { None, Menu, Seating, Help, Controls, Online, Backup };
  enum class OnlinePage { Main, Setup, Join, Lobby, Link, Events };
  enum class TextField { None, Name, Token, Code };
  struct Toast {
    std::string text;
    uint64_t until_ns;
  };
  struct MenuItem {
    std::string label, value;
    int id;
    std::string key = {};  // its keyboard shortcut, shown next to it
  };
  // A clickable area, recorded while drawing and tested against the mouse on the next events.
  struct Hit {
    float x, y, w, h;
    int kind, arg;
    bool overlay;  // drawn by an overlay: only overlay areas react while one is open
  };
  struct ControlsEntry {
    int control;  // a Control, or kEntryReset / kEntryDone
    int column, row;
  };

  bool init(std::string& error);
  void shutdown();
  void loop();
  void handle(const SDL_Event& e);
  void handle_home(const SDL_Event& e);
  void handle_menu(const SDL_Event& e);
  bool handle_hotkey(const SDL_Event& e);
  void handle_mouse(const SDL_Event& e);
  bool handle_capture(const SDL_Event& e);  // Controls screen waiting for a key or button
  void hover(const Hit& h);
  void click(const Hit& h, int clicks = 1);
  const Hit* hit_at(float x, float y) const;
  void add_hit(float x, float y, float w, float h, int kind, int arg = 0);
  bool mouse_over(float x, float y, float w, float h) const;
  bool mouse_recent() const;
  void ui_action(int action);  // shared by keyboard and gamepad: up/down/left/right/accept/back/menu
  void draw();
  void draw_home();
  void draw_menu();
  void draw_seating();
  void draw_help();
  void draw_controls();
  // A rounded button; returns its width. `primary` fills it red; a disabled one is dim and inert.
  float draw_button(float x, float y, const std::string& label, float s, bool primary, bool focused, int kind,
                    int arg = 0, bool right_aligned = false, bool enabled = true);
  void open_controls();
  void close_controls();
  void controls_action(int action);
  std::vector<ControlsEntry> controls_entries() const;

  // Online play (app_online.cpp).
  void open_online();
  void draw_online();
  void draw_online_status();  // in-game: waiting / paused notices
  float online_button(float x, float y, const std::string& label, float s, bool primary, int id, bool right = false,
                      bool enabled = true);
  void draw_field(float x, float y, float w, float s, TextField field, const std::string& value,
                  const std::string& placeholder, bool masked);
  void online_click(int id);
  void online_action(int action);
  bool handle_text(const SDL_Event& e);  // a text field has the keyboard
  void focus_field(TextField field);
  void start_setup();
  void finish_setup();
  void host_online();
  void join_online();
  // GBA link play (link.hpp): the game whose link it is (running, else picked in the library).
  const Game* link_game() const;
  std::string link_mode(const Game& game) const;  // "cable", "wireless" or "auto"
  void host_link();
  void join_link();
  // Event gifts (GBA Pokémon): the app as an event's distribution machine (gift_station.hpp).
  std::vector<EventGift> events_for(const Game& game) const;
  void open_event(const EventGift& gift);
  void close_event();
  void update_event();
  // A link needs the link hardware the game was started with: restart it (keeping its progress)
  // when an event left the Wireless Adapter in.
  void ensure_link_hardware();
  void start_online_game();
  void end_online_game();  // host: end for everyone, keep playing; guest: leave
  void process_net_events();
  void run_online_frames(uint64_t frame_ns);
  bool game_sha(const Game& game, std::array<uint8_t, 32>& out);
  std::string versions() const;
  std::string relay_url() const;
  std::string group_key() const;
  bool has_group_key() const;
  std::filesystem::path group_key_path() const;
  void load_group_key();
  bool store_group_key(const std::string& group, const std::string& relay, const std::string& key);
  void forget_group_key();
  bool import_key_file(const std::filesystem::path& file);
  bool save_key_file(std::filesystem::path file);
  void choose_key_file();
  void choose_key_save();
  std::vector<int> local_seats() const;
  bool online_blocks(int menu_id);  // explains and returns true when a menu action is off online
  // Google Drive backup (app_backup.cpp).
  void init_backup();
  std::string game_title(const std::string& folder) const;
  void open_backup();
  void draw_backup();
  void draw_backup_status(float right, float y, float s);  // library header: "Backed up 12 minutes ago"
  void backup_click(int id);
  void backup_action(int action);
  void process_backup();
  void backup_on_exit();
  bool backup_auto() const;
  bool backup_states() const;
  void draw_toasts();
  void draw_players_strip(float x, float y, float s);

  bool start_game(const Game& game);
  void stop_game();
  void restart_game_keep_progress();
  void apply_core_options(const Game& game);
  Look current_look() const;
  int fill_mode(const Game* game) const;  // the game's fill mode, never one that breaks it
  // Rendering size for a game: a fixed factor, or 0 to render at exactly render_height.
  int resolution_factor(const std::string& game_id) const;
  int render_height(const std::string& game_id) const;
  bool supersampling(const std::string& game_id) const;
  int texture_look(const std::string& game_id) const;
  void run_frames();
  int refreshes_per_frame(double fps) const;
  void check_speed(double fps);

  void load_game_save();
  void sync_game_save(bool force);
  void save_state();
  void load_state();
  std::filesystem::path state_path(int slot) const;

  void choose_games_folder();
  void choose_save_to_import();
  void import_save(const std::filesystem::path& file);
  void rescan();
  void set_fullscreen(bool on);
  void toast(const std::string& text, double seconds = 3.0);
  void take_screenshot(const std::filesystem::path& file);
  std::vector<MenuItem> menu_items() const;
  void menu_change(int id, int direction);

  Options opt_;
  SDL_Window* window_ = nullptr;
  SDL_GLContext gl_ = nullptr;
  Core core_;
  Video video_;
  Audio audio_;
  Input input_;
  Ui ui_;
  Settings settings_;
  std::filesystem::path pref_dir_, assets_dir_, library_dir_, gba_library_dir_;
  // Each console's emulator core; the one loaded now. Between games the N64 core is loaded (online
  // play compares its version).
  std::filesystem::path n64_core_path_, gba_core_path_;
  System loaded_system_ = System::N64;
  // Loads `system`'s core unless it is loaded already. Returns an error message, or empty.
  std::string use_core(System system);
  // Where a console's saves live: Memories\N64, Memories\GBA.
  const std::filesystem::path& library_for(System system) const {
    return system == System::GBA ? gba_library_dir_ : library_dir_;
  }
  std::vector<Game> games_;
  int selected_ = 0;
  std::optional<Game> game_;
  Screen screen_ = Screen::Home;
  Overlay overlay_ = Overlay::None;
  int menu_index_ = 0;
  int home_first_row_ = 0;
  std::vector<Hit> hits_;
  bool drawing_overlay_ = false;
  float mouse_x_ = -1, mouse_y_ = -1;  // pixels
  uint64_t mouse_seen_ns_ = 0;
  int controls_tab_ = 0;  // 0-2: keyboard layouts, 3: controllers
  int controls_index_ = 0;
  bool controls_capture_ = false;
  Overlay controls_return_ = Overlay::None;

  // Online play.
  Netplay net_;
  Link link_;
  GiftStation gifts_;
  std::string event_key_;  // the open event's gift ("aurora"); empty when none
  GiftStation::Stage event_stage_ = GiftStation::Stage::Closed;
  OnlinePage online_page_ = OnlinePage::Main;
  TextField text_field_ = TextField::None;
  std::string name_input_, token_input_, code_input_, online_note_;
  bool confirm_remove_key_ = false;  // "Remove key" asks once more before forgetting the group
  std::string group_name_, group_relay_, group_key_;  // the online group, from the shared key file
  std::vector<int> online_buttons_;  // buttons drawn on the current online page, for keyboard focus
  int online_focus_ = 0;
  std::thread setup_thread_;
  std::mutex setup_mutex_;
  std::string setup_progress_;
  bool setup_finished_ = false;
  cloudflare::Result setup_result_;
  std::atomic<bool> setup_running_{false};
  std::optional<Game> hosted_game_;
  bool online_game_ = false;       // the running game is (or is about to be) an online one
  bool guest_session_ = false;     // ...and this computer is a guest: the host's save, nothing written here
  bool net_game_pending_ = false;  // loaded, waiting for everyone before frame 0
  std::vector<uint8_t> guest_save_;
  uint64_t stalled_since_ns_ = 0;
  uint64_t stalled_ns_ = 0;  // time spent waiting for others' inputs, logged every 10 seconds of play
  int stalls_ = 0;
  bool printed_invite_ = false, printed_link_ = false;
  std::map<std::string, std::array<uint8_t, 32>> sha_cache_;

  // Google Drive backup.
  Backup backup_;
  std::vector<int> backup_buttons_;
  int backup_focus_ = 0;
  bool confirm_disconnect_ = false;
  std::vector<Backup::Found> backup_found_;
  uint64_t backup_found_ns_ = 0;
  bool test_synced_ = false;
  bool backup_was_working_ = false;
  uint64_t next_backup_ns_ = 0;  // the hourly backup
  // Two ways: a game that starts is checked against Drive at once, in the background.
  std::string drive_check_key_;    // the game to check next ("GBA/<game>" or the N64 game's folder)
  uint64_t game_started_ns_ = 0;
  bool session_saved_ = false;     // the running game has saved since it started
  int slot_ = 1;
  bool running_ = true;
  bool fullscreen_ = false;
  bool vsync_working_ = false;
  // Frame pacing: the screen's refresh period as measured, and whether recent refreshes were missed.
  double refresh_ns_ = 0;
  uint64_t last_swap_ns_ = 0, refreshes_ = 0;
  int unlocked_for_ = 0;  // refreshes left before pacing locks to the screen again after a slow patch
  uint64_t speed_since_ns_ = 0, speed_frames_ = 0;  // keeping up: frames run since then
  bool fast_ = false;  // a GBA game sped up (the speed key held): its sound is left out meanwhile
  int slow_spans_ = 0;      // slow 5-second stretches in a row (one alone may be the game loading)
  bool slow_told_ = false;  // said once this game that the computer can't keep up
  int out_w_ = 0, out_h_ = 0;
  uint64_t last_ns_ = 0, acc_ns_ = 0;
  uint64_t frame_count_ = 0;
  std::deque<Toast> toasts_;
  std::vector<uint8_t> last_saved_ram_;
  std::vector<uint8_t> pending_ram_;
  uint64_t pending_since_frame_ = 0;
  uint32_t user_event_ = 0;
  bool back_held_ = false;
};

const char* preset_name(Preset p);

}  // namespace p64
