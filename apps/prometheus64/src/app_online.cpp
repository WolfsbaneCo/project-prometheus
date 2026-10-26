// Online play: the screens (start, Cloudflare setup, join, lobby), the in-game notices and the
// glue between the lockstep engine (netplay.cpp) and the running game. Brief section 11.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "app.hpp"
#include "app_shared.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr std::size_t kSrmSize = 0x48800;

// Buttons on the online screens (kHitOnline arg).
enum OnlineButton {
  kObHost, kObJoin, kObSetup, kObClose, kObBack, kObSignup, kObTokenPage, kObPasteToken, kObInstall, kObFixPage,
  kObPasteCode, kObJoinGo, kObCopyCode, kObStart, kObLeave, kObDelayDown, kObDelayUp, kObDone
};

std::string default_name() {
  for (const char* var : {"USERNAME", "USER"}) {
    if (const char* v = std::getenv(var); v && *v) return std::string(v).substr(0, 24);
  }
  return "Player";
}

bool hex_to_bytes(const std::string& hex, std::array<uint8_t, 32>& out) {
  if (hex.size() != 64) return false;
  auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
  for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(nib(hex[2 * i]) << 4 | nib(hex[2 * i + 1]));
  return true;
}

std::string ping_words(int ms) {
  if (ms < 0) return "measuring...";
  const std::string n = std::to_string(ms) + " ms";
  if (ms < 40) return n + "  -  great";
  if (ms < 80) return n + "  -  good";
  if (ms < 120) return n + "  -  fine for slower games";
  return n + "  -  hard for fast games";
}

Color ping_colour(int ms) {
  if (ms < 0) return kFaint;
  if (ms < 80) return rgb(0x2FB45A);
  if (ms < 120) return rgb(0xF2C12E);
  return kRed;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Setup and helpers

std::string App::versions() const {
  const char* core = core_.info().library_version;
  return std::string("Prometheus 64 ") + P64_VERSION + " / core " + (core ? core : "?");
}

std::string App::hosting_relay_url() const {
  if (const char* v = std::getenv("P64_RELAY_URL"); v && *v) return v;  // tests and private relays
  if (const std::string custom = settings_.get("relay_url"); !custom.empty()) return custom;
  if (const std::string sub = settings_.get("relay_subdomain"); !sub.empty()) return cloudflare::relay_url_for(sub);
  return {};
}

std::string App::invite_code() const {
  const std::string sub = settings_.get("relay_subdomain");
  const bool custom = std::getenv("P64_RELAY_URL") || !settings_.get("relay_url").empty();
  return net_.code() + (custom || sub.empty() ? std::string() : "." + sub);
}

std::vector<int> App::local_seats() const {
  std::vector<int> seats;
  for (int i = 0; i < Input::kPlayers; ++i) {
    if (!input_.seat(i).empty()) seats.push_back(i);
  }
  return seats;
}

bool App::game_sha(const Game& game, std::array<uint8_t, 32>& out) {
  const std::string key = utf8(game.path) + "|" + game.entry;
  if (auto it = sha_cache_.find(key); it != sha_cache_.end()) {
    out = it->second;
    return true;
  }
  std::vector<uint8_t> data;
  if (!load_game_data(game, data).empty()) return false;
  if (!hex_to_bytes(savekit::sha256_hex(data), out)) return false;
  sha_cache_[key] = out;
  return true;
}

void App::open_online() {
  if (name_input_.empty()) name_input_ = settings_.get("player_name", default_name());
  overlay_ = Overlay::Online;
  online_focus_ = 0;
  online_note_.clear();
  online_page_ = net_.active() ? OnlinePage::Lobby : OnlinePage::Main;
}

void App::focus_field(TextField field) {
  if (text_field_ == field) return;
  if (text_field_ == TextField::Name && !name_input_.empty()) {
    settings_.set("player_name", name_input_);
    settings_.save();
  }
  text_field_ = field;
  if (field == TextField::None) {
    SDL_StopTextInput(window_);
  } else {
    SDL_StartTextInput(window_);
  }
}

bool App::handle_text(const SDL_Event& e) {
  if (text_field_ == TextField::None || overlay_ != Overlay::Online) return false;
  std::string* target = text_field_ == TextField::Name ? &name_input_ : text_field_ == TextField::Token ? &token_input_ : &code_input_;
  const std::size_t limit = text_field_ == TextField::Name ? 24 : text_field_ == TextField::Token ? 200 : 40;
  auto add = [&](const char* text) {
    for (const char* c = text; *c; ++c) {
      const unsigned char ch = static_cast<unsigned char>(*c);
      if (text_field_ == TextField::Code && !(std::isalnum(ch) || ch == '.' || ch == '-')) continue;
      if (text_field_ == TextField::Token && std::isspace(ch)) continue;
      if (ch < 0x20) continue;
      if (target->size() < limit) target->push_back(static_cast<char>(ch));
    }
  };
  if (e.type == SDL_EVENT_TEXT_INPUT) {
    add(e.text.text);
    return true;
  }
  if (e.type != SDL_EVENT_KEY_DOWN) return false;
  const SDL_Scancode sc = e.key.scancode;
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return false;  // shortcuts still work
  if (sc == SDL_SCANCODE_BACKSPACE) {
    while (!target->empty() && (static_cast<unsigned char>(target->back()) & 0xC0) == 0x80) target->pop_back();
    if (!target->empty()) target->pop_back();
  } else if (sc == SDL_SCANCODE_V && (e.key.mod & SDL_KMOD_CTRL)) {
    if (char* clip = SDL_GetClipboardText()) {
      add(clip);
      SDL_free(clip);
    }
  } else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) {
    const TextField f = text_field_;
    focus_field(TextField::None);
    if (f == TextField::Token) start_setup();
    if (f == TextField::Code) join_online();
  } else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_TAB) {
    focus_field(TextField::None);
  }
  return true;  // typing never moves the menus
}

void App::start_setup() {
  if (setup_running_) return;
  if (setup_thread_.joinable()) setup_thread_.join();
  setup_running_ = true;
  setup_finished_ = false;
  setup_result_ = {};
  {
    std::lock_guard<std::mutex> lock(setup_mutex_);
    setup_progress_ = "Starting...";
  }
  const std::string token = token_input_;
  setup_thread_ = std::thread([this, token] {
    cloudflare::Result r = cloudflare::install_relay(token, [this](const std::string& line) {
      std::lock_guard<std::mutex> lock(setup_mutex_);
      setup_progress_ = line;
    });
    std::lock_guard<std::mutex> lock(setup_mutex_);
    setup_result_ = r;
    setup_finished_ = true;
    setup_running_ = false;
  });
}

void App::finish_setup() {
  cloudflare::Result r;
  {
    std::lock_guard<std::mutex> lock(setup_mutex_);
    if (!setup_finished_) return;
    setup_finished_ = false;
    r = setup_result_;
  }
  if (setup_thread_.joinable()) setup_thread_.join();
  if (r.ok) {
    settings_.set("relay_subdomain", r.subdomain);
    settings_.save();
    token_input_.clear();  // the key is never kept
  }
}

// ---------------------------------------------------------------------------------------------
// Hosting and joining

void App::host_online() {
  if (games_.empty()) {
    online_note_ = "Add your games first: Choose games folder on the library screen";
    return;
  }
  const std::string url = hosting_relay_url();
  if (url.empty()) {
    online_page_ = OnlinePage::Setup;
    online_focus_ = 0;
    return;
  }
  const Game& g = hosted_game_ ? *hosted_game_ : games_[std::clamp(selected_, 0, static_cast<int>(games_.size()) - 1)];
  Netplay::GameInfo info;
  info.id = g.id;
  info.title = g.title;
  if (!game_sha(g, info.sha)) {
    online_note_ = "Could not read " + g.title;
    return;
  }
  hosted_game_ = g;
  focus_field(TextField::None);
  const auto seats = local_seats();
  uint8_t rumble = 0;
  for (std::size_t k = 0; k < seats.size(); ++k) {
    if (input_.has_rumble(seats[k])) rumble |= static_cast<uint8_t>(1u << k);
  }
  net_.host(url, name_input_.empty() ? default_name() : name_input_, info, static_cast<int>(std::max<std::size_t>(1, seats.size())),
            rumble, versions());
  printed_invite_ = false;
  online_page_ = OnlinePage::Lobby;
  online_focus_ = 0;
}

void App::join_online() {
  // An invite is CODE.subdomain (the host's own relay) or just CODE (a relay set up here).
  std::string text;
  for (char c : code_input_) {
    if (!std::isspace(static_cast<unsigned char>(c))) text += c;
  }
  const auto dot = text.find('.');
  std::string code = text.substr(0, dot);
  std::string sub = dot == std::string::npos ? std::string() : text.substr(dot + 1);
  for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  for (char& c : sub) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (code.size() != 5) {
    online_note_ = "An invite code looks like K7Q2M.alex: five letters or numbers, a dot, then a name";
    return;
  }
  std::string url;
  if (const char* v = std::getenv("P64_RELAY_URL"); v && *v) {
    url = v;
  } else if (!sub.empty()) {
    url = cloudflare::relay_url_for(sub);
  } else {
    url = hosting_relay_url();
  }
  if (url.empty()) {
    online_note_ = "Ask your friend for the whole code: it has a dot and a name after the five letters";
    return;
  }
  focus_field(TextField::None);
  const auto seats = local_seats();
  uint8_t rumble = 0;
  for (std::size_t k = 0; k < seats.size(); ++k) {
    if (input_.has_rumble(seats[k])) rumble |= static_cast<uint8_t>(1u << k);
  }
  net_.join(url, code, name_input_.empty() ? default_name() : name_input_,
            static_cast<int>(std::max<std::size_t>(1, seats.size())), rumble, versions());
  online_note_.clear();
  online_page_ = OnlinePage::Lobby;
  online_focus_ = 0;
}

void App::start_online_game() {
  std::string why;
  if (!net_.can_start(why) || !hosted_game_) {
    toast(why, 4);
    return;
  }
  online_game_ = true;
  guest_session_ = false;
  if (!start_game(*hosted_game_)) {
    online_game_ = false;
    return;
  }
  std::vector<uint8_t> save;
  if (uint8_t* ram = core_.save_ram(); ram && core_.save_ram_size() == kSrmSize) save.assign(ram, ram + kSrmSize);
  // Brief 11.5: keep a copy of the host's save from before the session.
  std::error_code ec;
  const fs::path dir = library_dir_ / game_->internal_name / "backups";
  fs::create_directories(dir, ec);
  const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
  std::ofstream(dir / ("before-online-" + std::to_string(stamp) + ".srm"), std::ios::binary)
      .write(reinterpret_cast<const char*>(save.data()), static_cast<std::streamsize>(save.size()));
  net_.start(save);
  net_game_pending_ = true;
  overlay_ = Overlay::Online;
  online_page_ = OnlinePage::Lobby;
}

void App::end_online_game() {
  const bool host = net_.is_host();
  net_.leave();
  input_.set_overrides(false);
  net_game_pending_ = false;
  online_game_ = false;
  if (host) {
    toast("Online game ended. You're playing offline now", 5);
  } else {
    stop_game();
    guest_session_ = false;
    toast("You left the online game", 4);
  }
}

bool App::online_blocks(int menu_id) {
  if (!online_game_ || !net_.active()) return false;
  switch (menu_id) {
    case kSaveState:
    case kLoadState:
      toast("Save states are off during online play: they would put the players out of sync", 4);
      return true;
    case kPicture:
    case kTextures:
    case kResolution:
      toast("Change this before going online: it restarts the game", 4);
      return true;
    case kReset:
      if (!net_.is_host()) {
        toast("Only the host can reset an online game", 4);
        return true;
      }
      net_.request_reset();
      toast("Resetting for everyone");
      overlay_ = Overlay::None;
      return true;
    case kQuit:
      end_online_game();
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------------------------
// Network events and the lockstep loop

void App::process_net_events() {
  finish_setup();
  if (!net_.active()) return;
  net_.update();
  for (auto& e : net_.take_events()) {
    switch (e.kind) {
      case Netplay::Event::Message:
        toast(e.text, 5);
        break;
      case Netplay::Event::CheckGame: {
        bool ok = false, found = false;
        for (const Game& g : games_) {
          if (g.id != e.game.id) continue;
          found = true;
          std::array<uint8_t, 32> sha{};
          if (game_sha(g, sha) && sha == e.game.sha) {
            ok = true;
            break;
          }
        }
        net_.report_game(ok, ok      ? std::string()
                             : found ? "Has a different version of " + e.game.title
                                     : "Doesn't have " + e.game.title);
        break;
      }
      case Netplay::Event::LoadGame: {
        const Game* game = nullptr;
        for (const Game& g : games_) {
          std::array<uint8_t, 32> sha{};
          if (g.id == e.game.id && game_sha(g, sha) && sha == e.game.sha) {
            game = &g;
            break;
          }
        }
        if (!game) {
          net_.leave();
          toast("Could not find " + e.game.title + " to start the online game", 6);
          break;
        }
        const Game chosen = *game;
        online_game_ = true;
        guest_session_ = true;
        guest_save_ = std::move(e.save);
        if (!start_game(chosen)) {
          net_.leave();
          online_game_ = guest_session_ = false;
          break;
        }
        net_game_pending_ = true;
        overlay_ = Overlay::Online;
        online_page_ = OnlinePage::Lobby;
        net_.report_ready();
        break;
      }
      case Netplay::Event::Begin:
        net_game_pending_ = false;
        overlay_ = Overlay::None;
        focus_field(TextField::None);
        input_.set_overrides(true);
        acc_ns_ = 0;
        last_ns_ = SDL_GetTicksNS();
        stalled_since_ns_ = 0;
        toast("Online game started  -  " + std::to_string(net_.delay()) + " frames of input delay", 4);
        break;
      case Netplay::Event::Ended:
        input_.set_overrides(false);
        net_game_pending_ = false;
        if (game_ && online_game_ && !e.keep_playing) stop_game();
        online_game_ = false;
        guest_session_ = false;
        toast(e.text, 6);
        if (overlay_ == Overlay::Online) {
          online_page_ = OnlinePage::Main;
          online_note_ = e.text;
        }
        break;
    }
  }
  if (!net_.active()) return;
  // Test mode hooks.
  if (opt_.online_host && !printed_invite_ && !net_.code().empty()) {
    std::printf("online-invite %s\n", invite_code().c_str());
    std::fflush(stdout);
    printed_invite_ = true;
  }
  std::string why;
  if (opt_.online_start && net_.is_host() && net_.phase() == Netplay::Phase::Lobby && net_.can_start(why)) {
    start_online_game();
  }
}

void App::run_online_frames(uint64_t frame_ns) {
  const bool host = net_.is_host();
  const bool menu_open = overlay_ != Overlay::None;
  // The host's menu pauses everyone (brief 11.7); a guest's menu doesn't hold up the others.
  if (host) net_.set_paused(menu_open);
  if (host && menu_open) {
    acc_ns_ = 0;
    return;
  }
  if (!opt_.screenshot.empty() || opt_.online_exit_frame) acc_ns_ = frame_ns;  // test mode: one frame per pass
  for (int runs = 0; acc_ns_ >= frame_ns && runs < 3; ++runs) {
    std::vector<PadState> local;
    for (int seat : local_seats()) local.push_back(menu_open ? PadState{} : input_.seat_state(seat));
    net_.sample_local(local);
    net_.flush();
    if (!net_.ready_to_run()) {
      net_.flush(true);
      if (!stalled_since_ns_) stalled_since_ns_ = SDL_GetTicksNS();
      acc_ns_ = std::min(acc_ns_, frame_ns);
      return;
    }
    stalled_since_ns_ = 0;
    for (int p = 0; p < Netplay::kPorts; ++p) input_.set_override(p, net_.input(p));
    if (net_.reset_due()) core_.reset();
    core_.run();
    acc_ns_ -= frame_ns;
    ++frame_count_;
    net_.frame_done(core_.system_ram(), core_.system_ram_size());
    if (host && frame_count_ % 30 == 0) sync_game_save(false);
    if (opt_.online_exit_frame && net_.frame() >= opt_.online_exit_frame) {
      const savekit::Bytes ram(core_.system_ram(), core_.system_ram() + core_.system_ram_size());
      std::printf("online-ram %u %s\n", net_.frame(), savekit::sha256_hex(ram).c_str());
      std::fflush(stdout);
      if (!opt_.screenshot.empty()) take_screenshot(opt_.screenshot);
      net_.flush(true);
      SDL_Delay(500);  // let the last inputs reach the others before leaving
      running_ = false;
      return;
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Screens

float App::online_button(float x, float y, const std::string& label, float s, bool primary, int id, bool right,
                         bool enabled) {
  const int index = static_cast<int>(online_buttons_.size());
  if (enabled) online_buttons_.push_back(id);
  const bool focused = enabled && text_field_ == TextField::None && index == online_focus_;
  return draw_button(x, y, label, s, primary, focused, kHitOnline, id, right, enabled);
}

void App::draw_field(float x, float y, float w, float s, TextField field, const std::string& value,
                     const std::string& placeholder, bool masked) {
  const float h = 52 * s;
  const bool focused = text_field_ == field;
  ui_.rect(x, y, w, h, rgb(0x0E0E10), 12 * s);
  ui_.outline(x, y, w, h, focused ? kRed : mouse_over(x, y, w, h) ? rgb(0xFFFFFF, 0.25f) : rgb(0xFFFFFF, 0.1f), 12 * s,
              focused ? 2 * s : 1.5f * s);
  std::string shown = value;
  if (masked && !value.empty()) {
    shown = std::string(std::min<std::size_t>(value.size(), 24), '*') + "  (" + std::to_string(value.size()) + " characters)";
  }
  const bool empty = shown.empty();
  if (empty) shown = placeholder;
  ui_.text(x + 18 * s, y + 13 * s, ui_.fit(shown, 21 * s, w - 40 * s), 21 * s, empty ? kFaint : kText);
  if (focused && (SDL_GetTicks() / 500) % 2 == 0) {
    const float cx = x + 18 * s + (empty ? 0 : ui_.measure(ui_.fit(shown, 21 * s, w - 40 * s), 21 * s)) + 2 * s;
    ui_.rect(cx, y + 12 * s, 2 * s, h - 24 * s, kText);
  }
  add_hit(x, y, w, h, kHitField, static_cast<int>(field));
}

void App::draw_online() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  online_buttons_.clear();
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.7f));
  const float pw = std::min(W - 40 * s, 1100 * s), ph = std::min(H - 40 * s, 860 * s);
  const float px = (W - pw) / 2, py = (H - ph) / 2;
  ui_.rect(px, py, pw, ph, kPanel, 24 * s);
  ui_.outline(px, py, pw, ph, rgb(0xFFFFFF, 0.06f), 24 * s, 1.5f * s);
  add_hit(px, py, pw, ph, kHitPanel);
  const float x = px + 48 * s, inner = pw - 96 * s;
  float y = py + 40 * s;
  auto title = [&](const std::string& t, const std::string& sub) {
    ui_.text(x, y, t, 36 * s, kText, true);
    y += 58 * s;
    for (const auto& line : wrap(ui_, sub, 20 * s, inner, false, 3)) {
      ui_.text(x, y, line, 20 * s, kDim);
      y += 30 * s;
    }
    y += 24 * s;
  };
  auto note = [&](float at_y) {
    if (online_note_.empty()) return;
    for (const auto& line : wrap(ui_, online_note_, 19 * s, inner, false, 2)) {
      ui_.text(x, at_y, line, 19 * s, rgb(0xF2C12E));
      at_y += 28 * s;
    }
  };
  const std::string relay = hosting_relay_url();

  if (online_page_ == OnlinePage::Main) {
    title("Play online", "Play with friends anywhere. Everyone needs Prometheus 64 and the same game in their library.");
    ui_.text(x, y + 14 * s, "Your name", 20 * s, kDim);
    draw_field(x + 160 * s, y, 380 * s, s, TextField::Name, name_input_, "Your name", false);
    y += 96 * s;
    // Host card.
    const std::string game = games_.empty() ? std::string()
                                            : games_[std::clamp(selected_, 0, static_cast<int>(games_.size()) - 1)].title;
    ui_.rect(x, y, inner, 150 * s, kTile, 18 * s);
    ui_.text(x + 28 * s, y + 26 * s, "Host a game", 26 * s, kText, true);
    ui_.text(x + 28 * s, y + 66 * s,
             ui_.fit(games_.empty() ? "Add your games first (Choose games folder on the library screen)"
                     : relay.empty() ? "One-time setup first (about 2 minutes), then you get an invite code for " + game
                                     : "Get an invite code for " + game + " to send to your friends",
                     19 * s, inner - 340 * s),
             19 * s, kDim);
    online_button(x + inner - 28 * s, y + 49 * s, relay.empty() ? "Set up and host" : "Host " + std::string(games_.empty() ? "" : "this game"),
                  s, true, kObHost, true, !games_.empty());
    y += 172 * s;
    // Join card.
    ui_.rect(x, y, inner, 150 * s, kTile, 18 * s);
    ui_.text(x + 28 * s, y + 26 * s, "Join a friend's game", 26 * s, kText, true);
    ui_.text(x + 28 * s, y + 66 * s, "Type the invite code your friend sent you. No setup needed", 19 * s, kDim);
    online_button(x + inner - 28 * s, y + 49 * s, "Join", s, true, kObJoin, true);
    y += 186 * s;
    ui_.text(x, y, relay.empty() ? "Online play isn't set up on this computer yet. Only hosts need it."
                                 : "Set up: your games go through " + relay.substr(relay.find("//") + 2),
             18 * s, kFaint);
    note(y + 36 * s);
    online_button(x, py + ph - 92 * s, relay.empty() ? "Set up online play" : "Set up again", s, false, kObSetup);
    online_button(px + pw - 48 * s, py + ph - 92 * s, "Close", s, false, kObClose, true);
    return;
  }

  if (online_page_ == OnlinePage::Setup) {
    title("Set up online play",
          "Online games go through a small relay on your own free Cloudflare account. You do this once, and only "
          "whoever hosts needs it. Your friends just type your invite code.");
    auto step = [&](int n, const std::string& head, const std::string& body, float h) {
      ui_.rect(x, y, 44 * s, 44 * s, kRed, 22 * s);
      ui_.text(x + 22 * s, y + 8 * s, std::to_string(n), 24 * s, kText, true, Align::Center);
      ui_.text(x + 68 * s, y + 4 * s, head, 23 * s, kText, true);
      float ly = y + 40 * s;
      for (const auto& line : wrap(ui_, body, 18 * s, inner - 360 * s, false, 3)) {
        ui_.text(x + 68 * s, ly, line, 18 * s, kDim);
        ly += 26 * s;
      }
      const float top = y;
      y += h;
      return top;
    };
    float t = step(1, "Make a free Cloudflare account", "Skip this if you already have one.", 96 * s);
    online_button(x + inner, t, "Open Cloudflare", s, false, kObSignup, true);
    t = step(2, "Create a key for Prometheus 64",
             "On the page that opens, scroll down and click Continue to summary, then Create Token, then Copy. "
             "If no permissions are filled in, use the template \"Edit Cloudflare Workers\".",
             126 * s);
    online_button(x + inner, t, "Open the key page", s, false, kObTokenPage, true);
    step(3, "Paste the key here", "It's used once to install the relay and never saved.", 76 * s);
    draw_field(x + 68 * s, y, inner - 68 * s - 300 * s, s, TextField::Token, token_input_, "Paste your key", true);
    online_button(x + inner - 140 * s, y, "Paste", s, false, kObPasteToken, true);
    online_button(x + inner, y, "Set up", s, true, kObInstall, true, !setup_running_ && !token_input_.empty());
    y += 84 * s;
    std::string progress;
    cloudflare::Result result;
    {
      std::lock_guard<std::mutex> lock(setup_mutex_);
      progress = setup_progress_;
      result = setup_result_;
    }
    if (setup_running_) {
      ui_.text(x, y, progress, 21 * s, kText);
    } else if (!result.message.empty()) {
      const Color c = result.ok ? rgb(0x2FB45A) : rgb(0xF2C12E);
      float ly = y;
      for (const auto& line : wrap(ui_, (result.ok ? "Done! " : "") + result.message, 21 * s, inner, false, 3)) {
        ui_.text(x, ly, line, 21 * s, c, result.ok);
        ly += 30 * s;
      }
      if (!result.fix_url.empty()) online_button(x, ly + 8 * s, "Open the fix page", s, false, kObFixPage);
    }
    online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
    if (!relay.empty()) online_button(px + pw - 48 * s, py + ph - 92 * s, "Host a game now", s, true, kObHost, true);
    return;
  }

  if (online_page_ == OnlinePage::Join) {
    title("Join a friend's game", "Type the invite code your friend sent you. It looks like K7Q2M.alex");
    draw_field(x, y, inner - 300 * s, s, TextField::Code, code_input_, "Invite code", false);
    online_button(x + inner - 140 * s, y, "Paste", s, false, kObPasteCode, true);
    online_button(x + inner, y, "Join", s, true, kObJoinGo, true, !code_input_.empty());
    y += 96 * s;
    note(y);
    online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
    return;
  }

  // Lobby.
  const bool host = net_.is_host();
  const auto phase = net_.phase();
  if (phase == Netplay::Phase::Connecting || phase == Netplay::Phase::Off) {
    title(host ? "Opening your game..." : "Joining...", net_.status().empty() ? online_note_ : net_.status());
    online_button(x, py + ph - 92 * s, "Back", s, false, kObLeave);
    return;
  }
  ui_.text(x, y, host ? "Your online game" : "Online game", 36 * s, kText, true);
  ui_.text(x + inner, y + 12 * s, ui_.fit(net_.game().title, 22 * s, inner * 0.5f), 22 * s, kDim, false, Align::Right);
  y += 70 * s;
  if (host) {
    ui_.rect(x, y, inner, 110 * s, kTile, 18 * s);
    ui_.text(x + 28 * s, y + 18 * s, "Invite code", 19 * s, kDim);
    ui_.text(x + 28 * s, y + 46 * s, invite_code(), 40 * s, kText, true);
    online_button(x + inner - 28 * s, y + 29 * s, "Copy", s, false, kObCopyCode, true);
    y += 124 * s;
    ui_.text(x, y, "Send this code to your friends. They choose Play online, then Join.", 19 * s, kDim);
    y += 46 * s;
  }
  ui_.text(x, y, "Players", 22 * s, kRed, true);
  y += 40 * s;
  const auto& ports = net_.ports();
  for (const auto& p : net_.players()) {
    std::string seats;
    for (int i = 0; i < Netplay::kPorts; ++i) {
      if (ports[i].player == p.id) seats += (seats.empty() ? "Player " : ", ") + std::to_string(i + 1);
    }
    ui_.rect(x, y, inner, 56 * s, kTile.with_alpha(0.7f), 12 * s);
    ui_.text(x + 20 * s, y + 15 * s, ui_.fit(p.name + (p.id == net_.my_id() ? "  (you)" : p.id == 0 ? "  (host)" : ""), 21 * s, inner * 0.35f),
             21 * s, kText, true);
    ui_.text(x + inner * 0.38f, y + 16 * s, seats.empty() ? (p.problem.empty() ? "Checking..." : "") : seats, 19 * s, kDim);
    if (!p.problem.empty()) {
      ui_.text(x + inner - 20 * s, y + 16 * s, ui_.fit(p.problem, 19 * s, inner * 0.4f), 19 * s, rgb(0xF2C12E), false, Align::Right);
    } else if (p.id != net_.my_id()) {
      ui_.text(x + inner - 20 * s, y + 16 * s, ping_words(p.ping_ms), 19 * s, ping_colour(p.ping_ms), false, Align::Right);
    }
    y += 64 * s;
  }
  y += 14 * s;
  const int d = net_.delay();
  ui_.text(x, y + 12 * s, "Input delay", 21 * s, kDim);
  const std::string dl = std::to_string(d) + " frames (" + std::to_string(d * 1000 / 60) + " ms)";
  if (host && phase == Netplay::Phase::Lobby) {
    online_button(x + 190 * s, y, "<", s, false, kObDelayDown);
    ui_.text(x + 270 * s, y + 12 * s, dl, 21 * s, kText);
    online_button(x + 270 * s + ui_.measure(dl, 21 * s) + 24 * s, y, ">", s, false, kObDelayUp);
  } else {
    ui_.text(x + 190 * s, y + 12 * s, dl, 21 * s, kText);
  }
  y += 66 * s;
  for (const auto& line : wrap(ui_,
                               "More delay copes with a slower connection but buttons react later. The suggested value "
                               "covers the slowest ping. Tip: a wired connection plays smoother than Wi-Fi.",
                               18 * s, inner, false, 3)) {
    ui_.text(x, y, line, 18 * s, kFaint);
    y += 26 * s;
  }
  y += 10 * s;
  std::string why;
  const bool can = host && net_.can_start(why);
  const std::string state = phase == Netplay::Phase::Starting ? (host ? "Starting: waiting for everyone to load the game..." : "Loading the game...")
                            : host ? (can ? "Everyone is ready" : why)
                                   : net_.status();
  ui_.text(x, y, ui_.fit(state, 20 * s, inner), 20 * s, can ? rgb(0x2FB45A) : kText);
  online_button(x, py + ph - 92 * s, host ? "Stop hosting" : "Leave", s, false, kObLeave);
  if (host) online_button(px + pw - 48 * s, py + ph - 92 * s, "Start", s, true, kObStart, true, can);
}

void App::draw_online_status() {
  if (!net_.running() || overlay_ != Overlay::None) return;
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_);
  std::string text;
  if (!net_.is_host() && net_.paused_by_host()) {
    text = "Paused by " + net_.player_name(0);
  } else if (stalled_since_ns_ && SDL_GetTicksNS() - stalled_since_ns_ > 300'000'000) {
    const std::string who = net_.waiting_for();
    text = "Waiting for " + (who.empty() ? std::string("the other players") : who) + "...";
  }
  if (text.empty()) return;
  const float w = ui_.measure(text, 24 * s, true) + 60 * s, h = 60 * s;
  ui_.rect((W - w) / 2, 40 * s, w, h, rgb(0x121214, 0.92f), 16 * s);
  ui_.text(W / 2, 40 * s + 16 * s, text, 24 * s, kText, true, Align::Center);
}

void App::online_click(int id) {
  switch (id) {
    case kObHost:
      online_note_.clear();
      host_online();
      break;
    case kObJoin:
      online_page_ = OnlinePage::Join;
      online_note_.clear();
      online_focus_ = 0;
      focus_field(TextField::Code);
      break;
    case kObSetup:
      online_page_ = OnlinePage::Setup;
      online_focus_ = 0;
      break;
    case kObClose:
      focus_field(TextField::None);
      overlay_ = Overlay::None;
      break;
    case kObBack:
      focus_field(TextField::None);
      online_page_ = OnlinePage::Main;
      online_focus_ = 0;
      break;
    case kObSignup: SDL_OpenURL(cloudflare::signup_url().c_str()); break;
    case kObTokenPage: SDL_OpenURL(cloudflare::token_page_url().c_str()); break;
    case kObPasteToken:
    case kObPasteCode:
      if (char* clip = SDL_GetClipboardText()) {
        std::string& target = id == kObPasteToken ? token_input_ : code_input_;
        target.clear();
        for (const char* c = clip; *c; ++c) {
          if (!std::isspace(static_cast<unsigned char>(*c))) target += *c;
        }
        SDL_free(clip);
      }
      break;
    case kObInstall:
      focus_field(TextField::None);
      start_setup();
      break;
    case kObFixPage: {
      std::lock_guard<std::mutex> lock(setup_mutex_);
      if (!setup_result_.fix_url.empty()) SDL_OpenURL(setup_result_.fix_url.c_str());
      break;
    }
    case kObJoinGo: join_online(); break;
    case kObCopyCode:
      SDL_SetClipboardText(invite_code().c_str());
      toast("Invite code copied");
      break;
    case kObStart: start_online_game(); break;
    case kObLeave:
      if (game_ && online_game_) {
        end_online_game();
      } else {
        net_.leave();
      }
      online_page_ = OnlinePage::Main;
      online_focus_ = 0;
      break;
    case kObDelayDown: net_.set_delay(net_.delay() - 1); break;
    case kObDelayUp: net_.set_delay(net_.delay() + 1); break;
    case kObDone: overlay_ = Overlay::None; break;
    default: break;
  }
}

void App::online_action(int action) {
  const int n = static_cast<int>(online_buttons_.size());
  switch (action) {
    case kUp:
    case kLeft:
      if (n) online_focus_ = (online_focus_ + n - 1) % n;
      break;
    case kDown:
    case kRight:
      if (n) online_focus_ = (online_focus_ + 1) % n;
      break;
    case kAccept:
      if (n) online_click(online_buttons_[std::clamp(online_focus_, 0, n - 1)]);
      break;
    case kBack:
    case kMenu:
      if (online_page_ == OnlinePage::Setup || online_page_ == OnlinePage::Join) {
        online_page_ = OnlinePage::Main;
        online_focus_ = 0;
      } else if (online_page_ == OnlinePage::Lobby && net_.phase() != Netplay::Phase::Running && !net_game_pending_) {
        // Closing the lobby screen keeps the room open; the library footer brings it back.
        overlay_ = Overlay::None;
      } else if (online_page_ != OnlinePage::Lobby) {
        overlay_ = Overlay::None;
      }
      break;
    default:
      break;
  }
}

}  // namespace p64
