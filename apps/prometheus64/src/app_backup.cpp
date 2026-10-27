// Google Drive backup: the Backup screen, the status line in the library, and the glue between the
// sync engine (backup.cpp) and the save library. Brief section 9.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "app.hpp"
#include "app_shared.hpp"
#include "google.hpp"
#include "savekit/library.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr uint64_t kHourNs = 3600ull * 1'000'000'000ull;
constexpr uint64_t kRetryNs = 300ull * 1'000'000'000ull;

// Buttons on the Backup screen (kHitBackup arg).
enum BackupButton {
  kBbConnect, kBbCancel, kBbNow, kBbOpenDrive, kBbDisconnect, kBbAuto, kBbStates, kBbClose, kBbStorage,
  kBbKeepHere = 100,   // + conflict index
  kBbUseThere = 200,   // + conflict index
};

std::string ago(int64_t when) {
  if (when <= 0) return "not yet";
  const int64_t s = static_cast<int64_t>(std::time(nullptr)) - when;
  if (s < 60) return "just now";
  if (s < 3600) return std::to_string(s / 60) + (s / 60 == 1 ? " minute ago" : " minutes ago");
  if (s < 86400) return std::to_string(s / 3600) + (s / 3600 == 1 ? " hour ago" : " hours ago");
  return std::to_string(s / 86400) + (s / 86400 == 1 ? " day ago" : " days ago");
}

// "2026-10-26T17:52:04Z" in this computer's time: "26 Oct, 18:52".
std::string local_time(const std::string& iso) {
  std::tm tm{};
  if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min,
                  &tm.tm_sec) != 6) {
    return "an unknown time";
  }
  tm.tm_year -= 1900;
  tm.tm_mon -= 1;
#if defined(_WIN32)
  const std::time_t t = _mkgmtime(&tm);
  std::tm local{};
  localtime_s(&local, &t);
#else
  const std::time_t t = timegm(&tm);
  std::tm local{};
  localtime_r(&t, &local);
#endif
  char buf[40];
  std::strftime(buf, sizeof buf, "%d %b, %H:%M", &local);
  return buf;
}

std::string device_name() {
  for (const char* var : {"COMPUTERNAME", "HOSTNAME"}) {
    if (const char* v = std::getenv(var); v && *v) return std::string(v).substr(0, 40);
  }
  return "This computer";
}

}  // namespace

// The library's folder name for a game is its internal name: show the library title when known.
std::string App::game_title(const std::string& folder) const {
  for (const Game& g : games_) {
    if (g.internal_name == folder) return g.title;
  }
  return folder;
}

void App::init_backup() {
  backup_.init(pref_dir_, library_dir_, device_name());
  if (opt_.backup_connect && !backup_.connected()) backup_.connect(true);
}

bool App::backup_auto() const { return settings_.get_int("drive_auto", 1) != 0; }
bool App::backup_states() const { return settings_.get_int("drive_states", 1) != 0; }

void App::open_backup() {
  overlay_ = Overlay::Backup;
  backup_found_ns_ = 0;
  backup_focus_ = 0;
  confirm_disconnect_ = false;
}

// Every frame: newer saves fetched from Drive go into the library (here, on the main thread), and
// the hourly backup starts when due.
void App::process_backup() {
  for (auto& dl : backup_.take_downloads()) {
    if (game_ && game_->internal_name == dl.game) {
      toast("Newer progress for " + game_title(dl.game) + " is on Google Drive. It's used next time you start the game", 6);
      continue;  // not while that game is running; the next backup fetches it again
    }
    try {
      auto save = savekit::load_current(dl.library, dl.game);
      if (!save) continue;
      savekit::StoreOptions o;
      o.library = library_dir_;
      o.game = dl.game;
      o.save = *save;
      o.source = "Google Drive (" + (dl.device.empty() ? std::string("another device") : dl.device) + ")";
      savekit::store_current(o);  // the save it replaces stays in history
      backup_.applied(dl.game, dl.id);
      toast("Picked up your latest " + game_title(dl.game) + " progress" + (dl.device.empty() ? "" : " from " + dl.device), 6);
    } catch (const std::exception& e) {
      toast(std::string("Could not use the save from Google Drive: ") + e.what(), 6);
    }
  }
  const uint64_t now = SDL_GetTicksNS();
  const bool working = backup_.state() == Backup::State::Working;
  if (backup_was_working_ && !working && !backup_.problem().empty() && !backup_.signed_out()) {
    next_backup_ns_ = now + kRetryNs;  // offline or a hiccup: try again soon, not in an hour (brief 9.2)
  }
  backup_was_working_ = working;
  if (!backup_.connected() || backup_.state() != Backup::State::Idle) return;
  if (opt_.backup_sync && !test_synced_) {
    test_synced_ = true;
    backup_.sync(backup_states());
    return;
  }
  if (!backup_auto()) return;
  if (next_backup_ns_ == 0) {
    next_backup_ns_ = now + kHourNs;
    if (opt_.screenshot.empty()) backup_.sync(backup_states());  // at start: pick up progress from other devices
  } else if (now >= next_backup_ns_) {
    next_backup_ns_ = now + kHourNs;
    backup_.sync(backup_states());
  }
}

// Brief 9.2: back up when the app closes.
void App::backup_on_exit() {
  if (backup_.state() == Backup::State::SigningIn) backup_.cancel_connect();  // nobody is coming back to it
  if (!backup_.connected()) {
    backup_.wait(2000);  // the network shuts down next: no worker may still be using it
    return;
  }
  const bool wanted = backup_auto() || opt_.backup_sync;
  if (!wanted && backup_.state() != Backup::State::Working) return;
  if (backup_.state() == Backup::State::Idle && ((backup_auto() && opt_.screenshot.empty()) || opt_.backup_sync)) {
    backup_.sync(backup_states());
  }
  if (backup_.state() == Backup::State::Working && window_ && gl_) {
    // One frame so the player knows why the window stays a moment.
    SDL_GetWindowSizeInPixels(window_, &out_w_, &out_h_);
    video_.begin_present(out_w_, out_h_);
    ui_.begin(out_w_, out_h_);
    const float s = out_h_ / 1080.0f;
    ui_.rect(0, 0, static_cast<float>(out_w_), static_cast<float>(out_h_), kBg1);
    ui_.text(out_w_ / 2.0f, out_h_ / 2.0f - 20 * s, "Backing up to Google Drive...", 30 * s, kText, true, Align::Center);
    ui_.end();
    SDL_GL_SwapWindow(window_);
  }
  backup_.wait(opt_.backup_sync ? 120000 : 30000);
}

void App::draw_backup_status(float right, float y, float s) {
  if (!backup_.connected()) return;
  std::string text;
  Color c = kFaint;
  if (backup_.state() == Backup::State::Working) {
    text = "Backing up to Google Drive...";
  } else if (!backup_.conflicts().empty()) {
    text = "Google Drive: a save needs your choice";
    c = rgb(0xF2C12E);
  } else if (!backup_.problem().empty()) {
    text = "Google Drive: not backed up";
    c = rgb(0xF2C12E);
  } else {
    text = "Backed up to Google Drive " + ago(backup_.last_sync());
  }
  const float w = ui_.measure(text, 16 * s) + 20 * s;
  if (mouse_over(right - w, y - 6 * s, w, 32 * s)) ui_.rect(right - w, y - 6 * s, w, 32 * s, rgb(0xFFFFFF, 0.07f), 10 * s);
  ui_.text(right - 10 * s, y, text, 16 * s, c, false, Align::Right);
  add_hit(right - w, y - 6 * s, w, 32 * s, kHitHint, kHintBackup);
}

void App::draw_backup() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  backup_buttons_.clear();
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.7f));
  const float pw = std::min(W - 40 * s, 1100 * s), ph = std::min(H - 40 * s, 900 * s);
  const float px = (W - pw) / 2, py = (H - ph) / 2;
  ui_.rect(px, py, pw, ph, kPanel, 24 * s);
  ui_.outline(px, py, pw, ph, rgb(0xFFFFFF, 0.06f), 24 * s, 1.5f * s);
  add_hit(px, py, pw, ph, kHitPanel);
  const float x = px + 48 * s, inner = pw - 96 * s;
  float y = py + 40 * s;
  auto button = [&](float bx, float by, const std::string& label, bool primary, int id, bool right = false) {
    const int index = static_cast<int>(backup_buttons_.size());
    backup_buttons_.push_back(id);
    return draw_button(bx, by, label, s, primary, index == backup_focus_, kHitBackup, id, right);
  };
  auto para = [&](const std::string& text, float size, Color c, int lines = 3) {
    for (const auto& line : wrap(ui_, text, size * s, inner, false, lines)) {
      ui_.text(x, y, line, size * s, c);
      y += size * 1.45f * s;
    }
  };
  ui_.text(x, y, "Back up to Google Drive", 36 * s, kText, true);
  y += 64 * s;

  if (!google::configured()) {
    para("Google Drive backup isn't switched on in this copy of Prometheus 64 yet: the app is still waiting to be "
         "registered with Google. It arrives in an update; your saves stay safe on this computer until then.", 21, kDim);
    button(px + pw - 48 * s, py + ph - 92 * s, "Close", false, kBbClose, true);
    return;
  }
  const Backup::State state = backup_.state();
  const std::string problem = backup_.problem();

  if (state == Backup::State::SigningIn) {
    para("Finish signing in in your browser. Choose the Google account for your saves and allow access to the "
         "files Prometheus 64 makes.", 21, kText);
    y += 10 * s;
    para("Prometheus 64 only sees its own N64 Saves folder, nothing else in your Drive.", 19, kDim);
    button(x, py + ph - 92 * s, "Cancel", false, kBbCancel);
    return;
  }

  if (!backup_.connected()) {
    para("Your saves are copied to a folder called N64 Saves in your Google Drive: quietly, when you close "
         "Prometheus 64 and every hour while it's open. Only saves go up, never games.", 21, kText);
    y += 12 * s;
    para("On another computer (or a future phone version), connect the same Google account to carry on where you "
         "left off. That uses your in-game saves; save states stay on the computer that made them.", 19, kDim);
    y += 12 * s;
    para("Tip: a personal Google account is a good home for game saves, separate from work.", 19, kDim);
    y += 24 * s;
    button(x, y, "Connect Google Drive", true, kBbConnect);
    y += 80 * s;
    if (!problem.empty()) para(problem, 19, rgb(0xF2C12E), 2);
    button(px + pw - 48 * s, py + ph - 92 * s, "Close", false, kBbClose, true);
    return;
  }

  // Connected.
  const std::string account = backup_.account();
  ui_.text(x, y, account.empty() ? "Connected to Google Drive" : "Connected as " + account, 22 * s, kText, true);
  y += 38 * s;
  ui_.text(x, y, "Your saves are in the N64 Saves folder in your Drive (you can move or rename it)", 19 * s, kDim);
  if (!backup_.root_id().empty()) button(x + inner, y - 12 * s, "Open in Drive", false, kBbOpenDrive, true);
  y += 56 * s;

  // Status.
  if (state == Backup::State::Working) {
    ui_.text(x, y, backup_.progress().empty() ? "Backing up..." : backup_.progress(), 21 * s, kText);
    y += 40 * s;
  } else if (!problem.empty()) {
    para(problem, 21, rgb(0xF2C12E), 2);
    if (backup_.signed_out()) button(x, y + 4 * s, "Connect again", true, kBbConnect);
    if (backup_.drive_full()) button(x, y + 4 * s, "Open Drive storage", false, kBbStorage);
    y += backup_.signed_out() || backup_.drive_full() ? 70 * s : 10 * s;
  } else if (backup_.last_sync() > 0) {
    ui_.text(x, y, "Backed up " + ago(backup_.last_sync()), 21 * s, rgb(0x2FB45A), true);
    y += 40 * s;
  }

  // Saves edited on two devices since they last agreed: the player picks; both stay in history.
  const auto conflicts = backup_.conflicts();
  for (std::size_t i = 0; i < conflicts.size() && i < 3; ++i) {
    const auto& c = conflicts[i];
    ui_.rect(x, y, inner, 146 * s, kTile, 16 * s);
    ui_.text(x + 22 * s, y + 16 * s, game_title(c.game) + ": saved on two devices", 21 * s, kText, true);
    ui_.text(x + 22 * s, y + 48 * s,
             ui_.fit("This computer: " + local_time(c.here_saved) + "     " +
                         (c.there_device.empty() ? std::string("Other device") : c.there_device) + ": " +
                         local_time(c.there_saved),
                     18 * s, inner - 44 * s),
             18 * s, kDim);
    float bx = x + 22 * s;
    bx += button(bx, y + 80 * s, "Keep this computer's", false, kBbKeepHere + static_cast<int>(i)) + 12 * s;
    button(bx, y + 80 * s, "Use " + (c.there_device.empty() ? std::string("the other") : c.there_device) + "'s",
           false, kBbUseThere + static_cast<int>(i));
    y += 160 * s;
  }
  if (!conflicts.empty()) {
    ui_.text(x, y, "Whichever you pick, the other save is kept in history.", 17 * s, kFaint);
    y += 36 * s;
  }

  // First time: what's here to back up (brief 9.3).
  if (backup_.last_sync() == 0 && state != Backup::State::Working) {
    if (SDL_GetTicksNS() - backup_found_ns_ > 2'000'000'000ull) {  // a folder scan: not every frame
      backup_found_ = backup_.found_here(backup_states());
      backup_found_ns_ = SDL_GetTicksNS();
    }
    const auto& found = backup_found_;
    if (found.empty()) {
      para("No saves on this computer yet. Back up now to fetch saves from your other devices.", 19, kDim);
    } else {
      ui_.text(x, y, "Found on this computer:", 19 * s, kDim);
      y += 32 * s;
      for (std::size_t i = 0; i < found.size() && i < 5; ++i) {
        const auto& f = found[i];
        std::string line = game_title(f.game) + ": " + std::to_string(f.saves) + (f.saves == 1 ? " save" : " saves");
        if (f.states) line += ", " + std::to_string(f.states) + (f.states == 1 ? " save state" : " save states");
        ui_.text(x + 18 * s, y, line, 19 * s, kText);
        y += 30 * s;
      }
      if (found.size() > 5) {
        ui_.text(x + 18 * s, y, "and " + std::to_string(found.size() - 5) + " more", 19 * s, kDim);
        y += 30 * s;
      }
    }
    y += 10 * s;
  }

  // Options.
  y = std::max(y + 10 * s, py + ph - 250 * s);
  auto toggle = [&](const std::string& label, bool on, int id) {
    ui_.text(x, y + 13 * s, label, 20 * s, kDim);
    button(x + inner, y, on ? "On" : "Off", on, id, true);
    y += 64 * s;
  };
  toggle("Back up automatically (when closing, and every hour)", backup_auto(), kBbAuto);
  toggle("Include save states (they stay with this computer)", backup_states(), kBbStates);

  float bx = x;
  if (state != Backup::State::Working) bx += button(bx, py + ph - 92 * s, "Back up now", true, kBbNow) + 14 * s;
  button(bx, py + ph - 92 * s, confirm_disconnect_ ? "Click again to disconnect" : "Disconnect", false, kBbDisconnect);
  button(px + pw - 48 * s, py + ph - 92 * s, "Close", false, kBbClose, true);
}

void App::backup_click(int id) {
  if (id != kBbDisconnect) confirm_disconnect_ = false;
  if (id >= kBbUseThere) {
    const auto c = backup_.conflicts();
    if (static_cast<std::size_t>(id - kBbUseThere) < c.size()) {
      backup_.choose(c[static_cast<std::size_t>(id - kBbUseThere)].game, false);
      backup_.sync(backup_states());
    }
    return;
  }
  if (id >= kBbKeepHere) {
    const auto c = backup_.conflicts();
    if (static_cast<std::size_t>(id - kBbKeepHere) < c.size()) {
      backup_.choose(c[static_cast<std::size_t>(id - kBbKeepHere)].game, true);
      backup_.sync(backup_states());
    }
    return;
  }
  switch (id) {
    case kBbConnect: backup_.connect(opt_.backup_connect); break;
    case kBbCancel: backup_.cancel_connect(); break;
    case kBbNow: backup_.sync(backup_states()); break;
    case kBbOpenDrive: SDL_OpenURL(("https://drive.google.com/drive/folders/" + backup_.root_id()).c_str()); break;
    case kBbStorage: SDL_OpenURL("https://one.google.com/storage"); break;
    case kBbDisconnect:
      if (!confirm_disconnect_) {
        confirm_disconnect_ = true;
        break;
      }
      confirm_disconnect_ = false;
      backup_.disconnect();
      toast("Disconnected. Your saves stay in Google Drive and on this computer", 5);
      break;
    case kBbAuto:
      settings_.set_int("drive_auto", backup_auto() ? 0 : 1);
      settings_.save();
      break;
    case kBbStates:
      settings_.set_int("drive_states", backup_states() ? 0 : 1);
      settings_.save();
      break;
    case kBbClose: overlay_ = Overlay::None; break;
    default: break;
  }
}

void App::backup_action(int action) {
  const int n = static_cast<int>(backup_buttons_.size());
  switch (action) {
    case kUp: case kLeft: if (n) backup_focus_ = (backup_focus_ + n - 1) % n; break;
    case kDown: case kRight: if (n) backup_focus_ = (backup_focus_ + 1) % n; break;
    case kAccept: if (n) backup_click(backup_buttons_[std::clamp(backup_focus_, 0, n - 1)]); break;
    case kBack: case kMenu: overlay_ = Overlay::None; break;
    default: break;
  }
}

}  // namespace p64
