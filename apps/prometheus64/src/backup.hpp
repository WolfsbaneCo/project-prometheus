// Google Drive backup of the save library (brief section 9).
//
// Drive holds a folder "N64 Saves" made by the app, laid out like the library:
//
//   N64 Saves/<GAME>/history/<time>__<id>/...   each in-game save, as the library keeps it (never changed)
//   N64 Saves/<GAME>/current.json               which save is the newest, from which device
//   N64 Saves/<GAME>/states/<device>/...        that device's save states (backup only)
//
// Saves are identified by their content (the library's ids), so devices agree on what is the same
// save. Each device remembers the last save it agreed with Drive on, per game. When only this
// device moved on, it uploads; when only another one did, it fetches that save so the player can
// carry on where they left off; when both did, the player chooses, and both stay in history.
// Cross-device resume uses in-game saves only: save states never move to another device.
//
// The work happens on a worker thread; the library itself is only ever written by the app's main
// thread (apply_download), never by the worker.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace p64 {

class Backup {
 public:
  enum class State { Off, SigningIn, Idle, Working };

  struct Conflict {
    std::string game;  // the library's folder name
    std::string here_saved, there_saved, there_device;  // ISO times, device name
  };
  // A newer save from another device, unpacked under `library` (as <library>/<game>/current/...).
  struct Download {
    std::string game, id, device;
    std::filesystem::path library;
  };
  struct Found {
    std::string game;
    int saves = 0, states = 0;
  };

  ~Backup();
  void init(const std::filesystem::path& pref_dir, const std::filesystem::path& library, const std::string& device_name);
  void set_library(const std::filesystem::path& library);

  // Main thread.
  void connect(bool fetch_in_test);  // opens the browser for Google's sign-in
  void cancel_connect();
  void disconnect();
  void sync(bool include_states);    // in the background, unless it is already running
  void wait(int timeout_ms);         // for the last backup when the app closes
  void choose(const std::string& game, bool keep_here);  // answer a conflict
  void applied(const std::string& game, const std::string& id);  // a Download is now the current save
  std::vector<Download> take_downloads();

  State state() const;
  bool connected() const;
  std::string account() const;
  std::string root_id() const;
  std::string progress() const;
  std::string problem() const;        // plain language, empty when all is well
  bool signed_out() const;            // the problem is fixed by connecting again
  bool drive_full() const;
  int64_t last_sync() const;          // unix seconds, 0 = never
  std::vector<Conflict> conflicts() const;
  std::vector<Found> found_here(bool include_states) const;  // what this computer has to back up

 private:
  struct GameState {
    std::string last_id;  // the save this device and Drive last agreed on
    std::string choice;   // "here" or "there" after a conflict was answered
  };
  void run_sync(bool include_states);
  void run_sign_in(bool fetch);
  void load_state();
  void save_state() const;  // under mutex_
  void set_problem(const std::string& text, int kind);
  void join();

  std::filesystem::path pref_dir_, library_, state_file_, incoming_;
  std::string device_id_, device_name_;
  mutable std::mutex mutex_;
  std::thread worker_;
  std::atomic<bool> busy_{false}, cancel_{false};
  State state_ = State::Off;
  std::string token_, account_, root_, progress_, problem_;
  int problem_kind_ = 0;  // 1 signed out, 2 Drive full
  int64_t last_sync_ = 0;
  std::map<std::string, GameState> games_;
  std::vector<Conflict> conflicts_;
  std::vector<Download> downloads_;
};

}  // namespace p64
