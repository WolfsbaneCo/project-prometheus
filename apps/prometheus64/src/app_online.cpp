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
#include <iterator>

#include "app.hpp"
#include "app_shared.hpp"
#include "json.hpp"
#include "savekit/rom_header.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr std::size_t kSrmSize = 0x48800;

// Buttons on the online screens (kHitOnline arg).
enum OnlineButton {
  kObHost, kObJoin, kObSetup, kObClose, kObBack, kObSignup, kObTokenPage, kObPasteToken, kObInstall, kObFixPage,
  kObPasteCode, kObJoinGo, kObCopyCode, kObStart, kObLeave, kObDelayDown, kObDelayUp, kObDone, kObAddKey, kObSaveKey,
  kObRemoveKey, kObLinkHost, kObLinkMode, kObLinkEnd, kObCopyLinkCode, kObEvents, kObEventClose,
  kObEventFirst  // kObEventFirst + i opens the i-th event of the game; + kObEventPrev/Next + row picks a choice
};
constexpr int kObEventPrev = 100, kObEventNext = 200;

// The Event gifts page's rows: a gift of its own, or the gifts of one group (picked with < >).
std::vector<std::vector<std::size_t>> event_rows(const std::vector<EventGift>& gifts) {
  std::vector<std::vector<std::size_t>> rows;
  for (std::size_t i = 0; i < gifts.size(); ++i) {
    if (!gifts[i].group.empty() && !rows.empty() && gifts[rows.back()[0]].group == gifts[i].group) {
      rows.back().push_back(i);
    } else {
      rows.push_back({i});
    }
  }
  return rows;
}

// Pokemon Ruby, Sapphire, Emerald, FireRed and LeafGreen (any region) by game code. They all
// trade and battle over the link cable; FireRed, LeafGreen and Emerald also over the Wireless
// Adapter (the Union Room). Ruby and Sapphire have no wireless.
bool pokemon_gen3(const Game& g, bool* wireless = nullptr) {
  if (g.system != System::GBA || g.id.size() < 7) return false;
  const std::string code = g.id.substr(4, 3);
  const bool w = code == "BPE" || code == "BPR" || code == "BPG";
  if (wireless) *wireless = w;
  return w || code == "AXV" || code == "AXP";
}

// The group's key file: which relay, and the key that opens it. Small JSON, and not tied to this
// app: every Project Prometheus app with online play (a GBA one trading Pokemon, say) uses the same
// key file and relay, each in its own rooms.
constexpr const char* kKeyFileType = "Project Prometheus online key";
constexpr const char* kKeyExt = ".prometheuskey";

struct GroupKey {
  std::string group, relay, key;
};

bool parse_key(const std::string& text, GroupKey& out) {
  json::Value v;
  if (!json::parse(text, v) || v["type"].str() != kKeyFileType) return false;
  out.relay = v["relay"].str();
  out.key = v["key"].str();
  out.group = v["group"].str().substr(0, 60);
  if (out.group.empty()) out.group = "your group";
  return out.key.size() >= 8 && (out.relay.rfind("wss://", 0) == 0 || out.relay.rfind("ws://", 0) == 0);
}

std::string key_json(const GroupKey& k) {
  return std::string("{\n  \"type\": ") + json::quote(kKeyFileType) + ",\n  \"version\": 1,\n  \"group\": " +
         json::quote(k.group) + ",\n  \"relay\": " + json::quote(k.relay) + ",\n  \"key\": " + json::quote(k.key) +
         "\n}\n";
}

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
  return std::string("Project Prometheus ") + P64_VERSION + " / core " + (core ? core : "?");
}

// The group this computer belongs to, from its key file (tests can point elsewhere).
std::string App::relay_url() const {
  if (const char* v = std::getenv("P64_RELAY_URL"); v && *v) return v;
  return group_relay_;
}

std::string App::group_key() const {
  if (const char* v = std::getenv("P64_RELAY_KEY"); v && *v) return v;
  return group_key_;
}

bool App::has_group_key() const { return !relay_url().empty() && !group_key().empty(); }

// One key per computer for every Project Prometheus app: kept next to the apps' own folders
// (".../Project Prometheus/online.prometheuskey"), so a later app finds it without asking again.
fs::path App::group_key_path() const {
  fs::path dir = pref_dir_;
  if (!dir.has_filename()) dir = dir.parent_path();  // the pref path ends in a separator
  return dir.parent_path() / ("online" + std::string(kKeyExt));
}

void App::load_group_key() {
  std::ifstream in(group_key_path(), std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  GroupKey k;
  if (!parse_key(text, k)) k = {};
  group_name_ = k.group;
  group_relay_ = k.relay;
  group_key_ = k.key;
}

bool App::store_group_key(const std::string& group, const std::string& relay, const std::string& key) {
  const fs::path file = group_key_path();
  const fs::path tmp = fs::path(file).concat(".tmp");
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out << key_json({group, relay, key});
    if (!out) return false;
  }
  fs::rename(tmp, file, ec);
  if (ec) return false;
  load_group_key();
  return true;
}

void App::forget_group_key() {
  std::error_code ec;
  fs::remove(group_key_path(), ec);
  load_group_key();
}

bool App::import_key_file(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  GroupKey k;
  if (!parse_key(text.substr(0, 4096), k)) {
    toast("That isn't a Project Prometheus key file", 5);
    return false;
  }
  if (!store_group_key(k.group, k.relay, k.key)) {
    toast("Could not keep the key on this computer", 5);
    return false;
  }
  toast("Added the key for " + group_name_ + ". You can host and join its online games now", 6);
  online_note_.clear();
  return true;
}

bool App::save_key_file(fs::path file) {
  if (!has_group_key()) return false;
  if (file.extension() != kKeyExt) file += kKeyExt;
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << key_json({group_name_.empty() ? std::string("Our group") : group_name_, relay_url(), group_key()});
  if (!out) {
    toast("Could not save the key file there", 5);
    return false;
  }
  toast("Key file saved. Send it to the friends you want to play with", 6);
  return true;
}

void App::choose_key_file() {
  static const SDL_DialogFileFilter filters[] = {{"Project Prometheus key file", "prometheuskey"}};
  auto* info = new std::pair<uint32_t, int>(user_event_, kKeyChosen);
  SDL_ShowOpenFileDialog(dialog_done, info, window_, filters, 1, nullptr, false);
}

void App::choose_key_save() {
  static const SDL_DialogFileFilter filters[] = {{"Project Prometheus key file", "prometheuskey"}};
  auto* info = new std::pair<uint32_t, int>(user_event_, kKeySaveAs);
  const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  const std::string where = docs ? utf8(from_utf8(docs) / ("Project Prometheus online key" + std::string(kKeyExt))) : std::string();
  SDL_ShowSaveFileDialog(dialog_done, info, window_, filters, 1, where.empty() ? nullptr : where.c_str());
}

std::vector<int> App::local_seats() const {
  std::vector<int> seats;
  for (int i = 0; i < Input::kPlayers; ++i) {
    if (!input_.seat(i).empty()) seats.push_back(i);
  }
  return seats;
}

// Fingerprint of the game as the console sees it: a .v64 (byte-swapped) or .n64 (word-reversed)
// copy, zipped or not, under any name, matches the same game as a .z64.
bool App::game_sha(const Game& game, std::array<uint8_t, 32>& out) {
  const std::string key = utf8(game.path) + "|" + game.entry;
  if (auto it = sha_cache_.find(key); it != sha_cache_.end()) {
    out = it->second;
    return true;
  }
  std::vector<uint8_t> data;
  if (!load_game_data(game, data).empty() || data.size() < 0x40) return false;
  const auto header = savekit::parse_rom_header(savekit::Bytes(data.begin(), data.begin() + 0x40));
  if (!header) return false;
  if (header->byte_order == savekit::RomByteOrder::ByteSwapped) {
    for (std::size_t i = 0; i + 1 < data.size(); i += 2) std::swap(data[i], data[i + 1]);
  } else if (header->byte_order == savekit::RomByteOrder::LittleEndian) {
    for (std::size_t i = 0; i + 3 < data.size(); i += 4) {
      std::swap(data[i], data[i + 3]);
      std::swap(data[i + 1], data[i + 2]);
    }
  }
  if (!hex_to_bytes(savekit::sha256_hex(data), out)) return false;
  sha_cache_[key] = out;
  return true;
}

void App::open_online() {
  if (name_input_.empty()) name_input_ = settings_.get("player_name", default_name());
  overlay_ = Overlay::Online;
  online_focus_ = 0;
  online_note_.clear();
  confirm_remove_key_ = false;
  online_page_ = link_.active()        ? OnlinePage::Link
                 : net_.active()       ? OnlinePage::Lobby
                 : !event_key_.empty() ? OnlinePage::Events
                                       : OnlinePage::Main;
}

// ---------------------------------------------------------------------------------------------
// GBA link play

const Game* App::link_game() const {
  if (game_) return game_->system == System::GBA ? &*game_ : nullptr;
  if (games_.empty()) return nullptr;
  const Game& g = games_[std::clamp(selected_, 0, static_cast<int>(games_.size()) - 1)];
  return g.system == System::GBA ? &g : nullptr;
}

std::string App::link_mode(const Game& game) const {
  bool wireless = false;
  if (!pokemon_gen3(game, &wireless)) return "auto";  // gpSP knows the other games' links
  // The cable by default: every generation 3 Pokemon game has it, so any two can link.
  const std::string m = settings_.game(game.id, "link", "cable");
  return wireless && m == "wireless" ? "wireless" : "cable";
}

void App::host_link() {
  const Game* g = link_game();
  if (!g || !has_group_key()) return;
  const Game game = *g;
  if (!game_ && !start_game(game)) return;
  ensure_link_hardware();
  link_.host(relay_url(), group_key(), name_input_.empty() ? default_name() : name_input_, link_mode(game), game.title);
  link_.attach(core_.netpacket());
  online_page_ = OnlinePage::Link;
  online_focus_ = 0;
}

void App::join_link() {
  const Game* g = link_game();
  if (!g || !has_group_key()) return;
  std::string code;
  for (char c : code_input_) {
    if (std::isalnum(static_cast<unsigned char>(c))) code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (code.size() != 5) {
    online_note_ = "A link code is five letters or numbers, like K7Q2M. Ask your friend for it";
    return;
  }
  focus_field(TextField::None);
  const Game game = *g;
  if (!game_ && !start_game(game)) return;
  ensure_link_hardware();
  link_.join(relay_url(), group_key(), code, name_input_.empty() ? default_name() : name_input_, link_mode(game),
             game.title);
  link_.attach(core_.netpacket());
  online_note_.clear();
  online_page_ = OnlinePage::Link;
  online_focus_ = 0;
}

// ---------------------------------------------------------------------------------------------
// Event gifts

std::vector<EventGift> App::events_for(const Game& game) const {
  if (game.system != System::GBA || game.id.size() < 8) return {};
  return event_gifts(game.id.substr(4, 4));
}

void App::open_event(const EventGift& gift) {
  const Game* g = link_game();
  if (!g) return;
  if (link_.active()) {
    online_note_ = "End the link first: the game's link port is busy";
    return;
  }
  // The game starts afresh at its title screen, with the Wireless Adapter in (or, for Ruby and
  // Sapphire, the link cable to the e-Reader): Mystery Gift (Mystery Events) is on the title menu.
  const Game game = *g;
  if (game_) stop_game();
  event_key_ = gift.key;
  event_cable_ = gift.cable;
  if (!start_game(game)) {
    event_key_.clear();
    event_cable_ = false;
    return;
  }
  if (event_cable_) {
    EventGift event = gift;
    if (gift.key == "jirachi") {
      // Made for this save, as the Bonus Disc did; once per Pokédex. The save is only read.
      const RubySapphireSave save = read_ruby_sapphire_save(core_.save_ram(), core_.save_ram_size());
      const char* refuse = !save.valid        ? "Jirachi: save your game in Ruby or Sapphire first"
                           : save.has_jirachi ? "Jirachi: this save's POKéDEX already has Jirachi caught"
                                              : nullptr;
      if (refuse) {
        event_key_.clear();
        event_cable_ = false;
        toast(refuse, 8);
        return;
      }
      event.script = jirachi_event(save.seed);
    }
    cable_.attach(core_.netpacket());
    cable_.open(event);
  } else {
    gifts_.attach(core_.netpacket());
    gifts_.open(gift);
  }
  event_stage_ = event_station_stage();
  overlay_ = Overlay::None;
  toast("Event open: " + gift.name +
            (event_cable_ ? ". In the game: MYSTERY EVENTS on the title menu, then press A"
                          : ". In the game: MYSTERY GIFT, WONDER CARDS, WIRELESS COMMUNICATION"),
        8);
}

void App::close_event() {
  if (event_key_.empty()) return;
  gifts_.close();
  gifts_.attach(nullptr);
  cable_.close();
  cable_.attach(nullptr);
  event_key_.clear();
  event_cable_ = false;
  event_stage_ = GiftStation::Stage::Closed;
}

GiftStation::Stage App::event_station_stage() const {
  if (!event_cable_) return gifts_.stage();
  switch (cable_.stage()) {
    case CableStation::Stage::Closed: return GiftStation::Stage::Closed;
    case CableStation::Stage::Waiting: return GiftStation::Stage::Waiting;
    case CableStation::Stage::Linked: return GiftStation::Stage::Linked;
    case CableStation::Stage::Sending: return GiftStation::Stage::Sending;
    case CableStation::Stage::Done: return GiftStation::Stage::Done;
    case CableStation::Stage::Failed: return GiftStation::Stage::Failed;
  }
  return GiftStation::Stage::Closed;
}

std::string App::event_station_status() const { return event_cable_ ? cable_.status() : gifts_.status(); }

void App::update_event() {
  if (event_cable_ ? !cable_.is_open() : !gifts_.is_open()) return;
  if (event_cable_) {
    cable_.update();
  } else {
    gifts_.update();
  }
  const GiftStation::Stage st = event_station_stage();
  if (st == event_stage_) return;
  event_stage_ = st;
  if (st == GiftStation::Stage::Done && event_cable_) {
    const std::string& k = event_key_;
    toast(k == "clock"                 ? "The clock fix went across. To set the clock, hold B, Select and Left on the title screen"
          : k == "words"               ? "The rare words went across: they're in your easy chat word lists"
          : k == "jirachi"             ? "Jirachi went across: it joins your party if there was room"
          : k.rfind("ecard-", 0) == 0  ? "The trainer went across: start a new Battle Tower challenge at that level to meet them"
                                       : "The event went across. After your game restarts, go and see your Dad at the Petalburg City Gym",
          10);
  } else if (st == GiftStation::Stage::Done && event_key_.rfind("visiting-", 0) == 0) {
    toast("Done. A new card: ask the deliveryman for the survey password. A new trainer: they're waiting in Sootopolis City", 10);
  } else if (st == GiftStation::Stage::Done && event_key_ == "stamp") {
    toast("Done: your game has the Stamp Card, or its next stamp. Connect again for another", 10);
  } else if (st == GiftStation::Stage::Done) {
    toast("Your game has the WONDER CARD. Collect the gift from the deliveryman upstairs in any Pokemon Center", 10);
  } else if (st == GiftStation::Stage::Failed) {
    toast("Event: " + event_station_status() +
              (event_cable_ ? ". Leave Mystery Events in the game and try again" : ". Close Mystery Gift in the game and try again"),
          8);
  }
}

void App::ensure_link_hardware() {
  close_event();
  stop_local_link();  // a game links to one place at a time: the relay now
  if (!game_ || game_->system != System::GBA) return;
  const std::string link = link_mode(*game_);
  const std::string want = link == "cable" ? "mul_poke" : link == "wireless" ? "rfu" : "auto";
  if (core_.option("gpsp_serial") != want) restart_game_keep_progress();
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
  const std::size_t limit = text_field_ == TextField::Name ? 24 : text_field_ == TextField::Token ? 200 : 5;
  auto add = [&](const char* text) {
    for (const char* c = text; *c; ++c) {
      const unsigned char ch = static_cast<unsigned char>(*c);
      if (text_field_ == TextField::Code && !std::isalnum(ch)) continue;
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
    const std::string who = name_input_.empty() ? default_name() : name_input_;
    if (!store_group_key(who + "'s group", r.relay_url, r.group_key)) toast("Could not keep the group key on this computer", 6);
    token_input_.clear();  // the Cloudflare key is never kept
  }
}

// ---------------------------------------------------------------------------------------------
// Hosting and joining

void App::host_online() {
  if (games_.empty()) {
    online_note_ = "Add your games first: Choose games folder on the library screen";
    return;
  }
  if (!has_group_key()) {
    online_note_ = "Add your group's key file first, or set up a new group";
    online_page_ = OnlinePage::Main;
    return;
  }
  const Game& g = hosted_game_ ? *hosted_game_ : games_[std::clamp(selected_, 0, static_cast<int>(games_.size()) - 1)];
  Netplay::GameInfo info;
  info.id = g.id;
  info.title = g.title;
  info.internal_name = g.internal_name;
  info.region = g.region;
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
  net_.host(relay_url(), group_key(), name_input_.empty() ? default_name() : name_input_, info,
            static_cast<int>(std::max<std::size_t>(1, seats.size())), rumble, versions());
  printed_invite_ = false;
  online_page_ = OnlinePage::Lobby;
  online_focus_ = 0;
}

void App::join_online() {
  // Joining takes both: the group's key (from its key file) and the host's five-character code.
  if (!has_group_key()) {
    online_note_ = "Add your group's key file first: ask whoever set up the group for it";
    online_page_ = OnlinePage::Main;
    return;
  }
  std::string code;
  for (char c : code_input_) {
    if (std::isalnum(static_cast<unsigned char>(c))) code += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  if (code.size() != 5) {
    online_note_ = "A game code is five letters or numbers, like K7Q2M. Ask the host for it";
    return;
  }
  focus_field(TextField::None);
  const auto seats = local_seats();
  uint8_t rumble = 0;
  for (std::size_t k = 0; k < seats.size(); ++k) {
    if (input_.has_rumble(seats[k])) rumble |= static_cast<uint8_t>(1u << k);
  }
  net_.join(relay_url(), group_key(), code, name_input_.empty() ? default_name() : name_input_,
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
        // Any copy of the same release works, whatever its file. Another region or revision, or a
        // changed copy (a hack, a damaged file), would play differently and fall out of step.
        bool ok = false, same_release = false;
        const Game* same_game = nullptr;
        for (const Game& g : games_) {
          if (g.id == e.game.id) {
            same_release = true;
            std::array<uint8_t, 32> sha{};
            if (game_sha(g, sha) && sha == e.game.sha) {
              ok = true;
              break;
            }
          } else if (!e.game.internal_name.empty() && g.internal_name == e.game.internal_name) {
            same_game = &g;
          }
        }
        std::string problem;
        if (ok) {
          problem.clear();
        } else if (same_release) {
          problem = "Has a changed copy of " + e.game.title + " (a hack or a damaged file)";
        } else if (same_game && same_game->region != e.game.region && !e.game.region.empty()) {
          problem = "Has the " + same_game->region + " version of " + e.game.title + ", the host has the " +
                    e.game.region + " one";
        } else if (same_game) {
          problem = "Has another release of " + e.game.title + " (a different revision)";
        } else {
          problem = "Doesn't have " + e.game.title;
        }
        net_.report_game(ok, problem);
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
        toast("Online game started  -  " + std::to_string(net_.delay()) + " frames of input delay, adjusting as you play", 4);
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
    std::printf("online-invite %s\n", net_.code().c_str());
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
      // An input that's a little late usually arrives within a few milliseconds: wait for it here,
      // up to half a frame, rather than leaving this whole frame empty.
      const uint64_t until = SDL_GetTicksNS() + frame_ns / 2;
      while (net_.running() && !net_.ready_to_run() && SDL_GetTicksNS() < until) {
        SDL_DelayNS(500'000);
        net_.update();
      }
    }
    if (net_.running() && net_.ready_to_run() && net_.take_yield()) {
      // This computer is running ahead of the others: one frame later, now and then, until level.
      acc_ns_ = std::min(acc_ns_, frame_ns);
      return;
    }
    if (!net_.ready_to_run()) {
      if (!stalled_since_ns_) stalled_since_ns_ = SDL_GetTicksNS();
      acc_ns_ = std::min(acc_ns_, frame_ns);
      return;
    }
    if (stalled_since_ns_) {
      stalled_ns_ += SDL_GetTicksNS() - stalled_since_ns_;
      ++stalls_;
    }
    stalled_since_ns_ = 0;
    if (net_.frame() % 600 == 0 && net_.frame()) {
      SDL_Log("online: frame %u, %d pauses, %llu ms waiting in the last 10 s (delay %d)", net_.frame(), stalls_,
              static_cast<unsigned long long>(stalled_ns_ / 1'000'000), net_.my_delay());
      stalls_ = 0;
      stalled_ns_ = 0;
    }
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
  // The Event gifts page grows for Emerald's nine rows; the other pages keep their size.
  const Game* events_game = online_page_ == OnlinePage::Events ? link_game() : nullptr;
  const bool tall = events_game && event_rows(events_for(*events_game)).size() > 5;
  const float pw = std::min(W - 40 * s, 1100 * s), ph = std::min(H - 40 * s, (tall ? 1000 : 860) * s);
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

  if (online_page_ == OnlinePage::Main) {
    const bool keyed = has_group_key();
    title("Play online", keyed ? "You're in " + (group_name_.empty() ? std::string("a group") : group_name_) +
                                     ". Host a game and send its code to friends in the group, or join with a code."
                               : "Online games happen in a private group. One person sets the group up once on a free "
                                 "Cloudflare account and shares the group's key file. Anyone with the key file can then "
                                 "host, or join with the host's game code.");
    ui_.text(x, y + 14 * s, "Your name", 20 * s, kDim);
    draw_field(x + 160 * s, y, 380 * s, s, TextField::Name, name_input_, "Your name", false);
    y += 96 * s;
    auto card = [&](const std::string& head, const std::string& body, const std::string& button, int id, bool enabled,
                    const std::string& more = {}) {
      ui_.rect(x, y, inner, 150 * s, kTile, 18 * s);
      ui_.text(x + 28 * s, y + 26 * s, head, 26 * s, kText, true);
      ui_.text(x + 28 * s, y + 66 * s, ui_.fit(body, 19 * s, inner - 340 * s), 19 * s, kDim);
      if (!more.empty()) ui_.text(x + 28 * s, y + 98 * s, ui_.fit(more, 18 * s, inner - 340 * s), 18 * s, kFaint);
      online_button(x + inner - 28 * s, y + 49 * s, button, s, true, id, true, enabled);
      y += 172 * s;
    };
    if (keyed && link_game()) {
      // A GBA game: a link, not a shared game. Each player plays their own game; only what goes
      // over the link travels, so nothing waits on the connection.
      const Game& g = *link_game();
      const std::string mode = link_mode(g);
      card("Host a link", "Link " + g.title + " with friends: you get a code to send them", "Host a link", kObLinkHost, true,
           "Each of you plays your own game at full speed; only the link travels");
      card("Join a link", "Type the code a friend sent you", "Join", kObJoin, true);
      bool wireless = false;
      if (pokemon_gen3(g, &wireless) && wireless) {
        card(mode == "wireless" ? "Link: Wireless Adapter" : "Link: cable",
             mode == "wireless" ? "The Union Room: FireRed, LeafGreen and Emerald only"
                                : "Trade and battle in the Pokemon Center: works with every Pokemon Advance game",
             mode == "wireless" ? "Use the cable" : "Use wireless", kObLinkMode, !link_.active(),
             "Everyone in the link needs the same");
      }
    } else if (keyed) {
      const std::string game = games_.empty() ? std::string()
                                              : games_[std::clamp(selected_, 0, static_cast<int>(games_.size()) - 1)].title;
      // The game hosted is the one picked in the library (one click on its tile, or the arrow keys).
      card("Host a game", games_.empty() ? "Add your games first (Choose games folder on the library screen)"
                                         : "Opens " + game + " and gives you a code to send to your friends",
           "Host this game", kObHost, !games_.empty(),
           games_.empty() ? std::string() : "Another game? Close, click it once in your library, then press N");
      card("Join a game", "Type the code the host sent you", "Join", kObJoin, true);
    } else {
      card("I have a key file", "Got a key file from a friend? Add it, then join or host", "Add key file", kObAddKey, true);
      card("Start a new group", "One-time setup on your free Cloudflare account, about 2 minutes", "Set up", kObSetup, true);
    }
    y += 14 * s;
    if (keyed) {
      ui_.text(x, y, "Friends need your group's key file (once) and the game code (each game).", 18 * s, kFaint);
    } else {
      ui_.text(x, y, "You can also drop a key file onto this window, or double-click it. The same key works in every "
                     "Project Prometheus app.", 18 * s, kFaint);
    }
    note(y + 36 * s);
    float bx = x;
    if (keyed) {
      bx += online_button(bx, py + ph - 92 * s, "Save key file", s, false, kObSaveKey) + 14 * s;
      online_button(bx, py + ph - 92 * s, confirm_remove_key_ ? "Click again to remove" : "Remove key", s, false,
                    kObRemoveKey);
    }
    const float close_w = online_button(px + pw - 48 * s, py + ph - 92 * s, "Close", s, false, kObClose, true);
    if (const Game* g = link_game(); g && !events_for(*g).empty())
      online_button(px + pw - 48 * s - close_w - 14 * s, py + ph - 92 * s, "Event gifts", s, false, kObEvents, true);
    return;
  }

  if (online_page_ == OnlinePage::Events) {
    const Game* g = link_game();
    const std::vector<EventGift> gifts = g ? events_for(*g) : std::vector<EventGift>{};
    title("Event gifts", "Gifts from Pokemon's old events, received the way players did: your game's own Mystery Gift "
                         "(Ruby and Sapphire: Mystery Events), from a distribution machine or e-Reader the app plays.");
    const auto rows = event_rows(gifts);
    const bool compact = rows.size() > 5;  // Emerald's nine: a little tighter, so the page still fits
    const float h = (compact ? 66 : 70) * s;  // a button (52) with even room above and below
    const float by = (h - 52 * s) / 2;
    for (std::size_t r = 0; r < rows.size(); ++r) {
      const std::vector<std::size_t>& row = rows[r];
      std::size_t pick = row[0];
      if (row.size() > 1) {
        const auto it = event_choice_.find(gifts[row[0]].group);
        pick = row[static_cast<std::size_t>(std::clamp(it == event_choice_.end() ? 0 : it->second, 0, static_cast<int>(row.size()) - 1))];
      }
      for (std::size_t i : row) {
        if (gifts[i].key == event_key_) pick = i;  // the open one
      }
      const EventGift& e = gifts[pick];
      const bool open = e.key == event_key_;
      ui_.rect(x, y, inner, h, open ? kTile.with_alpha(1.0f) : kTile, 14 * s);
      if (open) ui_.outline(x, y, inner, h, kRed, 14 * s, 2 * s);
      ui_.text(x + 24 * s, y + (compact ? 8 : 9) * s, e.name, (compact ? 21 : 22) * s, kText, true);
      ui_.text(x + 24 * s, y + (compact ? 37 : 39) * s, ui_.fit(e.where, (compact ? 16 : 17) * s, inner - (row.size() > 1 && !open ? 580 : 240) * s),
               (compact ? 16 : 17) * s, kDim);
      float right = x + inner - 20 * s;
      if (open) {
        right -= online_button(right, y + by, "Close event", s, false, kObEventClose, true);
      } else {
        right -= online_button(right, y + by, "Open event", s, false, kObEventFirst + static_cast<int>(pick), true,
                               !link_.active());
      }
      if (row.size() > 1 && !open) {
        // The choice: < Mareep >
        right -= 14 * s;
        right -= online_button(right, y + by, ">", s, false, kObEventFirst + kObEventNext + static_cast<int>(r), true);
        const float box = std::max(ui_.measure(e.choice, 19 * s, true), 150 * s) + 20 * s;
        ui_.text(right - box / 2, y + by + 15 * s, e.choice, 19 * s, kText, true, Align::Center);  // level with the buttons' text
        right -= box;
        online_button(right, y + by, "<", s, false, kObEventFirst + kObEventPrev + static_cast<int>(r), true);
      }
      y += h + 8 * s;
    }
    if (!event_key_.empty()) {
      ui_.text(x, y + 4 * s, "Event open: " + event_station_status(), 20 * s, kRed, true);
      y += 40 * s;
    } else {
      y += 8 * s;
    }
    const bool ruby_sapphire = g && g->id.size() >= 8 && (g->id.substr(4, 3) == "AXV" || g->id.substr(4, 3) == "AXP");
    const char* steps_ruby_sapphire[] = {
        "1.  Once: join the Mystery Event Club. Once you have 5 badges, talk to the man in any Pokemon Center who asks "
        "for your profile, and give it as MYSTERY EVENT IS EXCITING. Then save.",
        "2.  Save your game, then open the event here: the game restarts at its title screen, linked by cable to an "
        "e-Reader the app plays, as players did with an e-Reader card.",
        "3.  In the game: MYSTERY EVENTS, then press A. The game loads the event, saves and restarts.",
        "4.  Then: your Dad at the Petalburg City Gym has the ticket and the berry. The README's guide has every step.",
    };
    const char* steps_wireless[] = {
        "1.  Once: unlock Mystery Gift in your game. In any Poke Mart, fill in the questionnaire on the counter with "
        "LINK TOGETHER WITH ALL (in Emerald, once you have your Pokedex), then save.",
        "2.  Save your game, then open an event here: the game restarts at its title screen, with the Wireless Adapter.",
        "3.  In the game: MYSTERY GIFT, then WONDER CARDS, then WIRELESS COMMUNICATION.",
        "4.  Upstairs in any Pokemon Center, the deliveryman hands you the gift or tells you what to do next. Tickets: "
        "show them at the harbor he names. One card at a time. The README's guide has every step.",
    };
    // The steps, above the buttons; when the rows leave no room for them, where to find them.
    const float steps_end = py + ph - 104 * s;
    std::vector<std::vector<std::string>> steps;
    float need = 0;
    for (const char* step : ruby_sapphire ? steps_ruby_sapphire : steps_wireless) {
      steps.push_back(wrap(ui_, step, 18 * s, inner, false, 3));
      need += 26 * s * static_cast<float>(steps.back().size()) + 4 * s;
    }
    if (y + need > steps_end) {
      ui_.text(x, y + 6 * s, "Every step, for each gift: the guide in the README (\"Pokemon: the lost events, restored\").", 18 * s, kDim);
      y += 36 * s;
    } else {
      for (const auto& lines : steps) {
        for (const auto& line : lines) {
          ui_.text(x, y, line, 18 * s, kDim);
          y += 26 * s;
        }
        y += 4 * s;
      }
    }
    note(y + 4 * s);
    online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
    if (game_) online_button(px + pw - 48 * s, py + ph - 92 * s, "Back to the game", s, true, kObDone, true);
    return;
  }

  if (online_page_ == OnlinePage::Setup) {
    title("Start a new group",
          "Online games go through a small relay on your own free Cloudflare account. You do this once. Then you "
          "save the group's key file and send it to your friends: only people with it can join your games.");
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
    t = step(2, "Create a key for Project Prometheus",
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
      if (result.ok) {
        ui_.text(x, ly + 4 * s, "Now save your group's key file and send it to the friends you want to play with.", 19 * s, kText);
        online_button(x, ly + 44 * s, "Save key file", s, true, kObSaveKey);
      }
    } else if (has_group_key()) {
      for (const auto& line : wrap(ui_, "You already have a group key. Setting up again makes a new one, and the old key "
                                        "file stops working: everyone would need the new one.", 19 * s, inner, false, 2)) {
        ui_.text(x, y, line, 19 * s, rgb(0xF2C12E));
        y += 28 * s;
      }
    }
    online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
    if (result.ok) online_button(px + pw - 48 * s, py + ph - 92 * s, "Host a game now", s, false, kObHost, true);
    return;
  }

  if (online_page_ == OnlinePage::Join) {
    if (link_game()) {
      title("Join a link", "Type the link code your friend sent you: five letters or numbers, like K7Q2M.");
    } else {
      title("Join a game", "Type the game code the host sent you: five letters or numbers, like K7Q2M.");
    }
    draw_field(x, y, inner - 300 * s, s, TextField::Code, code_input_, "Game code", false);
    online_button(x + inner - 140 * s, y, "Paste", s, false, kObPasteCode, true);
    online_button(x + inner, y, "Join", s, true, kObJoinGo, true, !code_input_.empty());
    y += 96 * s;
    note(y);
    online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
    return;
  }

  if (online_page_ == OnlinePage::Link) {
    const auto lp = link_.phase();
    if (lp != Link::Phase::Open) {
      title("Link", lp == Link::Phase::Connecting ? link_.status() : "The link has ended.");
      note(y);
      online_button(x, py + ph - 92 * s, "Back", s, false, kObBack);
      return;
    }
    title("Link", "Each of you plays your own game; only the link travels. In Pokemon, go to a Pokemon Center's "
                  "upstairs to trade or battle. The link stays open while you play.");
    if (link_.is_host()) {
      ui_.rect(x, y, inner, 110 * s, kTile, 18 * s);
      ui_.text(x + 28 * s, y + 18 * s, "Link code", 19 * s, kDim);
      ui_.text(x + 28 * s, y + 46 * s, link_.code(), 40 * s, kText, true);
      online_button(x + inner - 28 * s, y + 29 * s, "Copy", s, false, kObCopyLinkCode, true);
      y += 124 * s;
      ui_.text(x, y, "Send this code to your friends. They need your group's key file too, then Play online > Join a link.",
               19 * s, kDim);
      y += 46 * s;
    }
    ui_.text(x, y, "Linked", 22 * s, kRed, true);
    y += 40 * s;
    auto row = [&](const std::string& name, const std::string& detail) {
      ui_.rect(x, y, inner, 56 * s, kTile.with_alpha(0.7f), 12 * s);
      ui_.text(x + 20 * s, y + 15 * s, ui_.fit(name, 21 * s, inner * 0.4f), 21 * s, kText, true);
      ui_.text(x + inner * 0.42f, y + 16 * s, ui_.fit(detail, 19 * s, inner * 0.55f), 19 * s, kDim);
      y += 64 * s;
    };
    auto mode_name = [](const std::string& m) {
      return m == "wireless" ? std::string("Wireless Adapter") : m == "cable" ? std::string("link cable") : std::string("link");
    };
    row((name_input_.empty() ? default_name() : name_input_) + "  (you)",
        (game_ ? game_->title : std::string()) + "  -  " + mode_name(link_.mode()));
    for (const auto& [id, p] : link_.peers()) row(p.name, p.game.empty() ? "Connecting..." : p.game + "  -  " + mode_name(p.mode));
    if (link_.peers().empty()) ui_.text(x, y + 8 * s, link_.is_host() ? "Waiting for a friend to join..." : "", 19 * s, kDim);
    note(py + ph - 150 * s);
    online_button(x, py + ph - 92 * s, "End link", s, false, kObLinkEnd);
    online_button(px + pw - 48 * s, py + ph - 92 * s, "Back to the game", s, true, kObDone, true);
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
    ui_.text(x + 28 * s, y + 18 * s, "Game code", 19 * s, kDim);
    ui_.text(x + 28 * s, y + 46 * s, net_.code(), 40 * s, kText, true);
    online_button(x + inner - 28 * s, y + 29 * s, "Copy", s, false, kObCopyCode, true);
    y += 124 * s;
    ui_.text(x, y, "Send this code to your friends. They need your group's key file too, then Play online > Join.", 19 * s, kDim);
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
                               "Where the game starts: during the game, each computer adjusts its own delay to its "
                               "connection, lower while it's steady (less lag) and higher when it wobbles (no pauses). "
                               "Tip: a wired connection plays smoother than Wi-Fi.",
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
  if (overlay_ != Overlay::None) return;
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_);
  if (!event_key_.empty() && game_ && event_stage_ != GiftStation::Stage::Done) {
    // An open event: what to do in the game, until the card has gone across.
    const std::string t = "Event open  -  " + event_station_status() +
                          (event_cable_ ? "  -  in the game: MYSTERY EVENTS on the title menu, then press A"
                                        : "  -  in the game: MYSTERY GIFT, WONDER CARDS, WIRELESS COMMUNICATION");
    const float w = ui_.measure(t, 20 * s) + 48 * s, h = 46 * s;
    ui_.rect((W - w) / 2, 24 * s, w, h, rgb(0x121214, 0.85f), 14 * s);
    ui_.text(W / 2, 24 * s + 12 * s, t, 20 * s, kText, false, Align::Center);
    return;
  }
  if (link_.active() && link_.peers().empty() && game_) {
    // A link nobody has joined yet: the code stays in sight until someone does.
    const std::string t = link_.phase() == Link::Phase::Connecting ? "Opening the link..."
                          : link_.is_host() ? "Link code " + link_.code() + "  -  waiting for a friend"
                                            : "Linking...";
    const float w = ui_.measure(t, 20 * s) + 48 * s, h = 46 * s;
    ui_.rect((W - w) / 2, 24 * s, w, h, rgb(0x121214, 0.85f), 14 * s);
    ui_.text(W / 2, 24 * s + 12 * s, t, 20 * s, kText, false, Align::Center);
    return;
  }
  if (!net_.running()) return;
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
  if (id != kObRemoveKey) confirm_remove_key_ = false;
  if (id >= kObEventFirst) {
    if (const Game* g = link_game()) {
      const std::vector<EventGift> gifts = events_for(*g);
      const int n = id - kObEventFirst;
      if (n >= kObEventPrev) {
        // A group's choice, one step back or on.
        const auto rows = event_rows(gifts);
        const std::size_t r = static_cast<std::size_t>(n % 100);
        if (r < rows.size() && rows[r].size() > 1) {
          const int count = static_cast<int>(rows[r].size());
          int& c = event_choice_[gifts[rows[r][0]].group];
          c = (c + (n >= kObEventNext ? 1 : -1) + count) % count;
        }
      } else if (static_cast<std::size_t>(n) < gifts.size()) {
        open_event(gifts[static_cast<std::size_t>(n)]);
      }
    }
    return;
  }
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
      online_note_.clear();
      break;
    case kObAddKey: choose_key_file(); break;
    case kObSaveKey: choose_key_save(); break;
    case kObRemoveKey:
      if (!confirm_remove_key_) {
        confirm_remove_key_ = true;
        break;
      }
      confirm_remove_key_ = false;
      forget_group_key();
      toast("Key removed from this computer. Add the key file again any time to play online", 5);
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
          const unsigned char ch = static_cast<unsigned char>(*c);
          if (id == kObPasteCode ? std::isalnum(ch) && target.size() < 5 : !std::isspace(ch)) target += *c;
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
    case kObJoinGo:
      if (link_game()) {
        join_link();
      } else {
        join_online();
      }
      break;
    case kObLinkHost:
      online_note_.clear();
      host_link();
      break;
    case kObEvents:
      online_page_ = OnlinePage::Events;
      online_note_.clear();
      online_focus_ = 0;
      break;
    case kObEventClose:
      close_event();
      toast("Event closed");
      break;
    case kObLinkMode:
      close_event();
      if (const Game* g = link_game(); g && !link_.active()) {
        settings_.set_game(g->id, "link", link_mode(*g) == "wireless" ? "cable" : "wireless");
        settings_.save();
        // The core picks its link when the game starts: restart a running one, keeping its progress.
        if (game_) restart_game_keep_progress();
      }
      break;
    case kObLinkEnd:
      link_.leave();
      toast("Link ended");
      online_page_ = OnlinePage::Main;
      online_focus_ = 0;
      break;
    case kObCopyLinkCode:
      SDL_SetClipboardText(link_.code().c_str());
      toast("Link code copied");
      break;
    case kObCopyCode:
      SDL_SetClipboardText(net_.code().c_str());
      toast("Game code copied");
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
      if (online_page_ == OnlinePage::Link) {
        overlay_ = Overlay::None;  // the link stays open: back to the game
      } else if (online_page_ == OnlinePage::Setup || online_page_ == OnlinePage::Join || online_page_ == OnlinePage::Events) {
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
