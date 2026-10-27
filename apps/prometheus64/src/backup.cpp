#include "backup.hpp"

#include <SDL3/SDL_misc.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <utility>

#include "google.hpp"
#include "json.hpp"
#include "net.hpp"
#include "savekit/sha256.hpp"
#include "secret_store.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr const char* kSecretName = "Prometheus 64/Google Drive";
constexpr const char* kRootName = "N64 Saves";
constexpr const char* kRootTag = "saves";  // the hidden property marking the folder (key "prometheus")
constexpr std::size_t kHistoryLimit = 10;  // brief 9.2: the last 10 in-game saves per game

std::string read_file(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool write_file(const fs::path& p, const std::string& data) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream out(p, std::ios::binary | std::ios::trunc);
  out << data;
  return static_cast<bool>(out);
}

std::string trim(std::string s) {
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
  return s;
}

std::string utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

fs::path from_utf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

// "<time>__<id>" or "<time>__<id>__replaced" -> id.
std::string entry_id(const std::string& name) {
  const auto a = name.find("__");
  if (a == std::string::npos) return {};
  const auto b = name.find("__", a + 2);
  return name.substr(a + 2, b == std::string::npos ? std::string::npos : b - a - 2);
}

std::string iso_now() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string random_hex(int n) {
  std::random_device rd;
  std::string s;
  for (int i = 0; i < n; ++i) s += "0123456789abcdef"[rd() & 15];
  return s;
}

// The library's games: folder name -> {saved versions by id, current id}.
struct LocalGame {
  std::map<std::string, std::string> entries;  // id -> folder name in history/
  std::string current, saved;                  // current save's id and when it was saved
  std::vector<fs::path> states;
};

std::map<std::string, LocalGame> scan_library(const fs::path& library) {
  std::map<std::string, LocalGame> games;
  std::error_code ec;
  if (!fs::is_directory(library, ec)) return games;
  for (const auto& g : fs::directory_iterator(library, ec)) {
    if (!g.is_directory(ec)) continue;
    LocalGame lg;
    for (const auto& h : fs::directory_iterator(g.path() / "history", ec)) {
      const std::string name = utf8(h.path().filename());
      if (!h.is_directory(ec) || (name.size() > 8 && name.substr(name.size() - 8) == ".partial")) continue;
      if (const std::string id = entry_id(name); !id.empty()) lg.entries.emplace(id, name);
    }
    ec.clear();
    lg.current = trim(read_file(g.path() / "current" / "id.txt"));
    json::Value v;
    if (json::parse(read_file(g.path() / "current" / "entry.json"), v)) lg.saved = v["saved"].str();
    for (const auto& s : fs::directory_iterator(g.path() / "states", ec)) {
      if (s.is_regular_file(ec)) lg.states.push_back(s.path());
    }
    ec.clear();
    if (!lg.entries.empty() || !lg.current.empty() || !lg.states.empty()) games[utf8(g.path().filename())] = std::move(lg);
  }
  return games;
}

using Drive = google::Drive;

const Drive::File* find(const std::vector<Drive::File>& files, const std::string& name) {
  for (const auto& f : files) {
    if (f.name == name) return &f;
  }
  return nullptr;
}

// Uploads a folder under a temporary name and renames it when everything is in, so another device
// never takes a half-uploaded save.
bool upload_dir(Drive& d, const std::string& parent, const fs::path& dir, const std::string& name) {
  std::string id;
  if (!d.create_folder(parent, name + ".partial", id)) return false;
  std::error_code ec;
  std::vector<fs::path> files, dirs;
  for (const auto& e : fs::directory_iterator(dir, ec)) (e.is_directory(ec) ? dirs : files).push_back(e.path());
  for (const auto& f : files) {
    std::string fid;
    if (!d.upload(id, utf8(f.filename()), read_file(f), fid)) return false;
  }
  for (const auto& sub : dirs) {
    std::string sid;
    if (!d.create_folder(id, utf8(sub.filename()), sid)) return false;
    for (const auto& e : fs::directory_iterator(sub, ec)) {
      std::string fid;
      if (e.is_regular_file(ec) && !d.upload(sid, utf8(e.path().filename()), read_file(e.path()), fid)) return false;
    }
  }
  return d.rename(id, name);
}

bool download_dir(Drive& d, const std::string& id, const fs::path& to) {
  std::vector<Drive::File> files;
  if (!d.list(id, files)) return false;
  for (const auto& f : files) {
    if (f.folder) {
      if (!download_dir(d, f.id, to / from_utf8(f.name))) return false;
    } else {
      std::string data;
      if (!d.download(f.id, data) || !write_file(to / from_utf8(f.name), data)) return false;
    }
  }
  return true;
}

}  // namespace

Backup::~Backup() {
  cancel_ = true;
  join();
}

void Backup::join() {
  if (worker_.joinable()) worker_.join();
}

void Backup::init(const fs::path& pref_dir, const fs::path& library, const std::string& device_name) {
  pref_dir_ = pref_dir;
  library_ = library;
  device_name_ = device_name;
  state_file_ = pref_dir / "google-drive.json";
  incoming_ = pref_dir / "drive-incoming";
  load_state();
  token_ = secret::load(kSecretName, pref_dir_);
  std::lock_guard<std::mutex> lock(mutex_);
  if (device_id_.empty()) {
    device_id_ = random_hex(12);
    save_state();
  }
  state_ = token_.empty() ? State::Off : State::Idle;
}

void Backup::set_library(const fs::path& library) {
  std::lock_guard<std::mutex> lock(mutex_);
  library_ = library;
}

void Backup::load_state() {
  json::Value v;
  if (!json::parse(read_file(state_file_), v)) return;
  device_id_ = v["device_id"].str();
  account_ = v["account"].str();
  root_ = v["root"].str();
  last_sync_ = static_cast<int64_t>(v["last_sync"].num());
  for (const auto& [name, g] : v["games"].object) games_[name] = {g["last"].str(), g["choice"].str()};
  for (const auto& c : v["conflicts"].array) {
    conflicts_.push_back({c["game"].str(), c["here"].str(), c["there"].str(), c["device"].str()});
  }
}

void Backup::save_state() const {
  std::string games, conflicts;
  for (const auto& [name, g] : games_) {
    games += std::string(games.empty() ? "" : ",\n    ") + json::quote(name) + ": {\"last\": " + json::quote(g.last_id) +
             ", \"choice\": " + json::quote(g.choice) + "}";
  }
  for (const auto& c : conflicts_) {
    conflicts += std::string(conflicts.empty() ? "" : ",\n    ") + "{\"game\": " + json::quote(c.game) +
                 ", \"here\": " + json::quote(c.here_saved) + ", \"there\": " + json::quote(c.there_saved) +
                 ", \"device\": " + json::quote(c.there_device) + "}";
  }
  write_file(state_file_, "{\n  \"device_id\": " + json::quote(device_id_) + ",\n  \"account\": " + json::quote(account_) +
                              ",\n  \"root\": " + json::quote(root_) + ",\n  \"last_sync\": " + std::to_string(last_sync_) +
                              ",\n  \"games\": {\n    " + games + "\n  },\n  \"conflicts\": [\n    " + conflicts + "\n  ]\n}\n");
}

// ---------------------------------------------------------------------------------------------
// Connecting

void Backup::connect(bool fetch_in_test) {
  if (busy_) return;
  join();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::SigningIn;
    problem_.clear();
    problem_kind_ = 0;
    progress_ = "Finish signing in in your browser...";
  }
  cancel_ = false;
  busy_ = true;
  worker_ = std::thread(&Backup::run_sign_in, this, fetch_in_test);
}

void Backup::cancel_connect() { cancel_ = true; }

void Backup::run_sign_in(bool fetch) {
  const auto open = [fetch](const std::string& url) {
    if (fetch) {
      // Tests: a stand-in for Google answers at once, so fetch the page here instead of a browser.
      std::thread([url] { net::request("GET", url, {}, {}, {}, 30); }).detach();
    } else {
      SDL_OpenURL(url.c_str());
    }
  };
  const google::SignIn r = google::sign_in(open, cancel_);
  std::string email;
  if (r.ok) {
    secret::save(kSecretName, r.refresh_token, pref_dir_);
    Drive d(r.refresh_token);
    d.account(email);
  }
  std::lock_guard<std::mutex> lock(mutex_);
  progress_.clear();
  if (r.ok) {
    token_ = r.refresh_token;
    account_ = email;
    state_ = State::Idle;
    problem_.clear();
    problem_kind_ = 0;
    save_state();
  } else {
    state_ = token_.empty() ? State::Off : State::Idle;
    problem_ = r.error;
  }
  busy_ = false;
}

void Backup::disconnect() {
  cancel_ = true;
  join();
  const std::string token = token_;
  google::revoke(token);
  secret::erase(kSecretName, pref_dir_);
  std::lock_guard<std::mutex> lock(mutex_);
  token_.clear();
  account_.clear();
  root_.clear();
  last_sync_ = 0;
  games_.clear();
  conflicts_.clear();
  downloads_.clear();
  problem_.clear();
  problem_kind_ = 0;
  state_ = State::Off;
  save_state();  // the files stay in Drive (brief 9.3)
}

// ---------------------------------------------------------------------------------------------
// Syncing

void Backup::sync(bool include_states) {
  if (busy_ || token_.empty()) return;
  join();
  cancel_ = false;
  busy_ = true;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::Working;
    progress_ = "Checking Google Drive...";
  }
  worker_ = std::thread(&Backup::run_sync, this, include_states);
}

void Backup::wait(int timeout_ms) {
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (busy_ && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
  if (busy_) cancel_ = true;
  join();
}

void Backup::set_problem(const std::string& text, int kind) {
  std::lock_guard<std::mutex> lock(mutex_);
  problem_ = text;
  problem_kind_ = kind;
}

void Backup::run_sync(bool include_states) {
  std::string token, root;
  fs::path library;
  std::map<std::string, GameState> games;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = token_;
    root = root_;
    library = library_;
    games = games_;
  }
  Drive d(token);
  std::vector<Conflict> conflicts;
  std::vector<Download> downloads;
  const auto progress = [this](const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = text;
  };
  const auto finish = [&](bool ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ok) {
      games_ = games;
      conflicts_ = conflicts;
      for (auto& dl : downloads) downloads_.push_back(std::move(dl));
      root_ = root;
      last_sync_ = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      problem_.clear();
      problem_kind_ = 0;
    } else if (!cancel_) {
      problem_ = d.error();
      problem_kind_ = d.problem() == Drive::Problem::SignedOut ? 1 : d.problem() == Drive::Problem::Full ? 2 : 0;
    }
    if (!root.empty()) root_ = root;
    progress_.clear();
    state_ = State::Idle;
    save_state();
    busy_ = false;
  };

  // The app's saves folder, wherever the player has moved it and whatever they renamed it: the one
  // this computer used before, else the one carrying the app's tag (made by another computer, perhaps),
  // else a new "N64 Saves" in My Drive. Only a folder in the bin or deleted is replaced.
  if (!root.empty() && !d.alive(root)) {
    if (d.problem() != Drive::Problem::None) return finish(false);
    root.clear();
  }
  if (root.empty() && !d.find_folder(kRootTag, kRootName, root)) return finish(false);
  if (root.empty() && !d.create_folder("root", kRootName, root, kRootTag)) return finish(false);
  std::vector<Drive::File> remote_games;
  if (!d.list(root, remote_games)) return finish(false);

  const auto local = scan_library(library);
  std::map<std::string, std::string> folders;  // game -> Drive folder id
  for (const auto& f : remote_games) {
    if (f.folder) folders.emplace(f.name, f.id);
  }
  std::vector<std::string> names;
  for (const auto& [name, lg] : local) names.push_back(name);
  for (const auto& [name, id] : folders) {
    if (!local.count(name)) names.push_back(name);
  }

  for (const std::string& game : names) {
    if (cancel_) return finish(false);
    static const LocalGame kNone;
    const LocalGame& lg = local.count(game) ? local.at(game) : kNone;
    GameState& gs = games[game];
    std::string folder = folders.count(game) ? folders[game] : std::string();
    if (folder.empty()) {
      if (lg.current.empty() && lg.states.empty()) continue;
      if (!d.create_folder(root, game, folder)) return finish(false);
    }
    std::vector<Drive::File> children, history_files;
    if (!d.list(folder, children)) return finish(false);
    std::string history = find(children, "history") ? find(children, "history")->id : std::string();
    if (history.empty() && !lg.entries.empty() && !d.create_folder(folder, "history", history)) return finish(false);
    std::map<std::string, std::pair<std::string, std::string>> remote_entries;  // id -> {folder id, name}
    if (!history.empty()) {
      if (!d.list(history, history_files)) return finish(false);
      for (const auto& f : history_files) {
        if (f.name.size() > 8 && f.name.substr(f.name.size() - 8) == ".partial") {
          d.remove(f.id);  // an upload that never finished
          continue;
        }
        if (const std::string id = entry_id(f.name); !id.empty()) remote_entries[id] = {f.id, f.name};
      }
    }
    // Drive's pointer to the newest save.
    std::string remote_id, remote_saved, remote_device, pointer_id;
    std::set<std::string> remote_seen;  // the saves the device behind Drive's newest one had: it carried on from them
    if (const auto* p = find(children, "current.json")) {
      pointer_id = p->id;
      std::string text;
      if (!d.download(p->id, text)) return finish(false);
      json::Value v;
      if (json::parse(text, v)) {
        remote_id = v["id"].str();
        remote_saved = v["saved"].str();
        remote_device = v["device"].str();
        for (const auto& id : v["seen"].array) remote_seen.insert(id.str());
      }
    }

    // Every saved version goes up once; they never change afterwards.
    for (const auto& [id, name] : lg.entries) {
      if (remote_entries.count(id)) continue;
      progress("Backing up " + game + "...");
      if (!upload_dir(d, history, library / from_utf8(game) / "history" / from_utf8(name), name)) return finish(false);
      remote_entries[id] = {"", name};
    }

    const auto push = [&]() {
      std::string seen;
      for (const auto& [id, name] : lg.entries) seen += (seen.empty() ? "" : ", ") + json::quote(id);
      const std::string pointer = "{\"id\": " + json::quote(lg.current) + ", \"saved\": " +
                                  json::quote(lg.saved.empty() ? iso_now() : lg.saved) + ", \"device\": " +
                                  json::quote(device_name_) + ", \"device_id\": " + json::quote(device_id_) +
                                  ", \"seen\": [" + seen + "]}\n";
      std::string id;
      const bool ok = pointer_id.empty() ? d.upload(folder, "current.json", pointer, id) : d.update(pointer_id, pointer);
      if (ok) {
        gs.last_id = lg.current;
        gs.choice.clear();
        remote_id = lg.current;
      }
      return ok;
    };
    const auto pull = [&]() {
      const auto it = remote_entries.find(remote_id);
      if (it == remote_entries.end() || it->second.first.empty()) return true;  // pointer without its save: wait
      progress("Getting " + game + " from " + (remote_device.empty() ? "Google Drive" : remote_device) + "...");
      const fs::path dir = incoming_ / from_utf8(game) / "current";
      std::error_code ec;
      fs::remove_all(incoming_ / from_utf8(game), ec);
      if (!download_dir(d, it->second.first, dir)) return false;
      downloads.push_back({game, remote_id, remote_device, incoming_});
      return true;
    };

    bool ok = true;
    const bool here_moved = !lg.current.empty() && lg.current != gs.last_id;
    const bool there_moved = !remote_id.empty() && remote_id != gs.last_id;
    if (lg.current.empty() && remote_id.empty()) {
      // Nothing saved yet (only save states, perhaps).
    } else if (gs.choice == "here" && !lg.current.empty()) {
      ok = push();
    } else if (gs.choice == "there" && !remote_id.empty()) {
      ok = pull();
    } else if (lg.current.empty()) {
      ok = pull();  // a new computer: carry on from Drive
    } else if (remote_id.empty() || lg.current == remote_id) {
      ok = lg.current == remote_id ? (gs.last_id = remote_id, true) : push();
    } else if (!there_moved || lg.entries.count(remote_id)) {
      ok = push();  // this computer carried on from Drive's newest save
    } else if (!here_moved || remote_seen.count(lg.current)) {
      ok = pull();  // another device carried on from this computer's save
    } else {
      conflicts.push_back({game, lg.saved, remote_saved, remote_device});
    }
    if (!ok) return finish(false);

    // Keep the last 10 versions in Drive too, never the one the pointer names.
    if (remote_entries.size() > kHistoryLimit) {
      std::vector<std::pair<std::string, std::string>> by_name;  // name -> folder id
      for (const auto& f : history_files) {
        if (!entry_id(f.name).empty() && entry_id(f.name) != remote_id) by_name.emplace_back(f.name, f.id);
      }
      std::sort(by_name.begin(), by_name.end());
      std::size_t extra = remote_entries.size() - kHistoryLimit;
      for (std::size_t i = 0; i < by_name.size() && extra > 0; ++i, --extra) d.remove(by_name[i].second);
    }

    // Save states: this device's own, as a backup (they never move to another device).
    if (include_states && !lg.states.empty()) {
      std::string states = find(children, "states") ? find(children, "states")->id : std::string();
      if (states.empty() && !d.create_folder(folder, "states", states)) return finish(false);
      std::vector<Drive::File> devices, files;
      if (!d.list(states, devices)) return finish(false);
      const std::string mine = device_name_ + " (" + device_id_.substr(0, 4) + ")";
      std::string dev = find(devices, mine) ? find(devices, mine)->id : std::string();
      if (dev.empty() && !d.create_folder(states, mine, dev)) return finish(false);
      if (!d.list(dev, files)) return finish(false);
      for (const auto& path : lg.states) {
        const std::string data = read_file(path);
        const std::string name = utf8(path.filename());
        const auto* f = find(files, name);
        const std::string sha = savekit::sha256_hex(savekit::Bytes(data.begin(), data.end()));
        if (f && f->sha256 == sha) continue;
        progress("Backing up " + game + " save states...");
        std::string id;
        if (!(f ? d.update(f->id, data) : d.upload(dev, name, data, id))) return finish(false);
      }
    }
  }
  finish(true);
}

void Backup::choose(const std::string& game, bool keep_here) {
  std::lock_guard<std::mutex> lock(mutex_);
  games_[game].choice = keep_here ? "here" : "there";
  conflicts_.erase(std::remove_if(conflicts_.begin(), conflicts_.end(), [&](const Conflict& c) { return c.game == game; }),
                   conflicts_.end());
  save_state();
}

void Backup::applied(const std::string& game, const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex_);
  games_[game].last_id = id;
  games_[game].choice.clear();
  save_state();
}

std::vector<Backup::Download> Backup::take_downloads() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::exchange(downloads_, {});
}

Backup::State Backup::state() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool Backup::connected() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !token_.empty();
}

std::string Backup::account() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return account_;
}

std::string Backup::root_id() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return root_;
}

std::string Backup::progress() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return progress_;
}

std::string Backup::problem() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return problem_;
}

bool Backup::signed_out() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return problem_kind_ == 1;
}

bool Backup::drive_full() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return problem_kind_ == 2;
}

int64_t Backup::last_sync() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_sync_;
}

std::vector<Backup::Conflict> Backup::conflicts() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return conflicts_;
}

std::vector<Backup::Found> Backup::found_here(bool include_states) const {
  fs::path library;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    library = library_;
  }
  std::vector<Found> out;
  for (const auto& [name, lg] : scan_library(library)) {
    Found f{name, static_cast<int>(lg.entries.size()), 0};
    if (include_states) {
      for (const auto& s : lg.states) f.states += s.extension() == ".state";
    }
    if (f.saves || f.states) out.push_back(f);
  }
  return out;
}

}  // namespace p64
