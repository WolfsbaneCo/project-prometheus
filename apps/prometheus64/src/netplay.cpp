#include "netplay.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "json.hpp"

namespace p64 {
namespace {

constexpr uint8_t kBroadcast = 255;
constexpr uint8_t kToRelay = 254;
constexpr int kRelayProtocol = 2;  // the relay's own version (relay/src/index.js), not this app's
constexpr const char* kApp = "prometheus64";  // this app's rooms on the group's relay (other apps have their own)  // a message for the relay itself: [254, 1] = the game started
constexpr int kBatch = 2;               // frames of input per message: halves the relay's message count
constexpr uint32_t kHashEvery = 300;    // compare memory fingerprints every 5 seconds
constexpr uint64_t kPingEveryNs = 1'000'000'000;

std::string url_encode(const std::string& s) {
  std::string out;
  static const char* hex = "0123456789ABCDEF";
  for (unsigned char c : s) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

// Little-endian message writing and reading. A reader that runs past the end stays failed and
// returns zeros, so a short or garbled message is simply ignored.
struct Writer {
  std::string b;
  Writer& u8(unsigned v) {
    b += static_cast<char>(v & 0xFF);
    return *this;
  }
  Writer& u16(unsigned v) { return u8(v).u8(v >> 8); }
  Writer& u32(uint32_t v) { return u16(v & 0xFFFF).u16(v >> 16); }
  Writer& u64(uint64_t v) { return u32(static_cast<uint32_t>(v)).u32(static_cast<uint32_t>(v >> 32)); }
  Writer& str(const std::string& s) {
    u16(static_cast<unsigned>(std::min<std::size_t>(s.size(), 0xFFFF)));
    b.append(s, 0, std::min<std::size_t>(s.size(), 0xFFFF));
    return *this;
  }
  Writer& bytes(const uint8_t* p, std::size_t n) {
    b.append(reinterpret_cast<const char*>(p), n);
    return *this;
  }
};

struct Reader {
  const std::string& b;
  std::size_t at = 0;
  bool ok = true;
  bool need(std::size_t n) {
    if (!ok || at + n > b.size()) ok = false;
    return ok;
  }
  unsigned u8() { return need(1) ? static_cast<uint8_t>(b[at++]) : 0; }
  unsigned u16() {
    const unsigned lo = u8();
    return lo | (u8() << 8);
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    return lo | (static_cast<uint32_t>(u16()) << 16);
  }
  uint64_t u64() {
    const uint64_t lo = u32();
    return lo | (static_cast<uint64_t>(u32()) << 32);
  }
  std::string str() {
    const unsigned n = u16();
    if (!need(n)) return {};
    std::string s = b.substr(at, n);
    at += n;
    return s;
  }
  bool bytes(uint8_t* out, std::size_t n) {
    if (!need(n)) return false;
    std::memcpy(out, b.data() + at, n);
    at += n;
    return true;
  }
};

// Inputs travel as 4 bytes per player: buttons, then the stick quantised to -127..127. Every
// computer, the one the player sits at included, uses the quantised value, so they all agree.
void put_pad(Writer& w, const PadState& p) {
  w.u16(p.buttons & 0xFFFF);
  w.u8(static_cast<uint8_t>(static_cast<int8_t>(std::lround(std::clamp(p.x, -1.0f, 1.0f) * 127.0f))));
  w.u8(static_cast<uint8_t>(static_cast<int8_t>(std::lround(std::clamp(p.y, -1.0f, 1.0f) * 127.0f))));
}

PadState get_pad(Reader& r) {
  PadState p;
  p.buttons = r.u16();
  p.x = static_cast<int8_t>(r.u8()) / 127.0f;
  p.y = static_cast<int8_t>(r.u8()) / 127.0f;
  return p;
}

PadState quantise(const PadState& p) {
  Writer w;
  put_pad(w, p);
  Reader r{w.b};
  return get_pad(r);
}

uint64_t fingerprint(const uint8_t* data, std::size_t size) {
  // FNV-1a over 64-bit words: fast enough for 8 MB every few seconds.
  uint64_t h = 1469598103934665603ull;
  std::size_t i = 0;
  for (; i + 8 <= size; i += 8) {
    uint64_t v;
    std::memcpy(&v, data + i, 8);
    h = (h ^ v) * 1099511628211ull;
  }
  for (; i < size; ++i) h = (h ^ data[i]) * 1099511628211ull;
  return h;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Lobby

void Netplay::open(const std::string& url, const std::string& name) {
  phase_ = Phase::Connecting;
  status_ = "Connecting...";
  my_name_ = name;
  players_.clear();
  ports_ = {};
  paks_ = {1, 1, 1, 1};
  code_.clear();
  game_checked_ = false;
  delay_chosen_ = false;
  desynced_ = false;
  host_paused_ = false;
  events_.clear();
  ws_.open(url);
}

void Netplay::host(const std::string& relay_url, const std::string& key, const std::string& name, const GameInfo& game,
                   int local_players, uint8_t rumble_mask, const std::string& versions) {
  game_ = game;
  versions_ = versions;
  my_local_players_ = std::clamp(local_players, 1, kPorts);
  my_rumble_ = rumble_mask;
  open(relay_url + "/v1/host?app=" + kApp + "&v=" + std::to_string(kRelayProtocol) + "&key=" + url_encode(key) +
           "&name=" + url_encode(name),
       name);
}

void Netplay::join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
                   int local_players, uint8_t rumble_mask, const std::string& versions) {
  game_ = {};
  versions_ = versions;
  my_local_players_ = std::clamp(local_players, 1, kPorts);
  my_rumble_ = rumble_mask;
  open(relay_url + "/v1/rooms/" + code + "?app=" + kApp + "&v=" + std::to_string(kRelayProtocol) + "&key=" + url_encode(key) +
           "&name=" + url_encode(name),
       name);
}

void Netplay::leave() {
  if (phase_ == Phase::Off) return;
  if (is_host() && phase_ != Phase::Connecting) {
    Writer w;
    w.u8(kEnd).str(my_name_ + " ended the online game");
    send(kBroadcast, w.b);
  }
  ws_.close();
  phase_ = Phase::Off;
  me_ = -1;
  status_.clear();
}

std::vector<Netplay::Event> Netplay::take_events() {
  std::vector<Event> out;
  out.swap(events_);
  return out;
}

void Netplay::send(int to, const std::string& body) {
  std::string msg;
  msg += static_cast<char>(to);
  msg += body;
  ws_.send(std::move(msg));
}

Netplay::Player* Netplay::player(int id) {
  for (auto& p : players_) {
    if (p.id == id) return &p;
  }
  return nullptr;
}

std::string Netplay::player_name(int id) const {
  for (const auto& p : players_) {
    if (p.id == id) return p.name;
  }
  return "Player";
}

void Netplay::update() {
  if (phase_ == Phase::Off) return;
  for (auto& e : ws_.poll()) {
    switch (e.kind) {
      case net::WebSocket::Event::Open: status_ = "Connected"; break;
      case net::WebSocket::Event::Text: on_text(e.data); break;
      case net::WebSocket::Event::Binary: on_binary(e.data); break;
      case net::WebSocket::Event::Closed: on_closed(e.data); break;
    }
    if (phase_ == Phase::Off) return;
  }
  // Ping everyone once a second: through the relay, so it measures the real path.
  const uint64_t now = SDL_GetTicksNS();
  if (phase_ != Phase::Connecting && now - last_ping_ns_ > kPingEveryNs) {
    last_ping_ns_ = now;
    Writer w;
    w.u8(kPing).u64(now);
    send(kBroadcast, w.b);
  }
}

void Netplay::on_text(const std::string& text) {
  json::Value v;
  if (!json::parse(text, v)) return;
  const std::string t = v["t"].str();
  if (t == "welcome") {
    me_ = static_cast<int>(v["you"].num());
    code_ = v["code"].str();
    phase_ = Phase::Lobby;
    Player self;
    self.id = me_;
    self.name = my_name_;
    self.ping_ms = 0;
    self.local_players = my_local_players_;
    self.rumble_mask = my_rumble_;
    self.checked = is_host();
    players_.push_back(self);
    for (const auto& p : v["players"].array) {
      Player other;
      other.id = static_cast<int>(p["id"].num());
      other.name = p["name"].str();
      players_.push_back(other);
    }
    if (is_host()) {
      status_ = "Waiting for friends to join";
      assign_ports();
    } else {
      status_ = "Joined. Checking versions...";
      Writer w;
      w.u8(kHello).u16(kProtocol).str(versions_).u8(static_cast<unsigned>(my_local_players_)).u8(my_rumble_);
      send(0, w.b);
    }
  } else if (t == "join") {
    Player p;
    p.id = static_cast<int>(v["id"].num());
    p.name = v["name"].str();
    if (!player(p.id)) players_.push_back(p);
    if (is_host()) {
      if (phase_ == Phase::Running || phase_ == Phase::Starting) {
        // Too late to join this game (the relay normally stops them first): tell them.
        Writer w;
        w.u8(kReject).str("This game has already started");
        send(p.id, w.b);
      } else {
        events_.push_back({Event::Message, p.name + " joined"});
      }
    }
  } else if (t == "leave") {
    const int id = static_cast<int>(v["id"].num());
    const std::string name = player_name(id);
    bool playing = false;  // drives a port in this game (not someone turned away for arriving late)
    for (const Port& port : ports_) playing = playing || port.player == id;
    players_.erase(std::remove_if(players_.begin(), players_.end(), [&](const Player& p) { return p.id == id; }),
                   players_.end());
    if (!is_host()) return;
    if (phase_ == Phase::Lobby) {
      events_.push_back({Event::Message, name + " left"});
      assign_ports();
      broadcast_lobby();
    } else if ((phase_ == Phase::Running || phase_ == Phase::Starting) && playing) {
      // A player leaving mid-game: end it for the others, and carry on offline.
      Writer w;
      w.u8(kEnd).str(name + " left the game");
      send(kBroadcast, w.b);
      end(name + " left. You're playing offline now", true);
    }
  }
}

void Netplay::on_closed(const std::string& reason) {
  if (phase_ == Phase::Off) return;
  const bool was_running = phase_ == Phase::Running || phase_ == Phase::Starting;
  if (is_host() && was_running) {
    end("Lost the connection to the relay. You're playing offline now", true);
  } else {
    end(reason.empty() ? "Disconnected" : reason, false);
  }
}

void Netplay::end(const std::string& reason, bool keep_playing) {
  Event e{Event::Ended, reason};
  e.keep_playing = keep_playing;
  events_.push_back(e);
  ws_.close();
  phase_ = Phase::Off;
  me_ = -1;
  status_.clear();
}

void Netplay::assign_ports() {
  // The host's players take the first ports, then each guest's in the order they joined.
  ports_ = {};
  std::vector<Player*> order;
  for (auto& p : players_) order.push_back(&p);
  std::stable_sort(order.begin(), order.end(), [](const Player* a, const Player* b) { return a->id < b->id; });
  int next = 0;
  for (Player* p : order) {
    if (p->id != 0 && !p->checked) continue;  // until it has said hello and checked the game
    for (int k = 0; k < p->local_players && next < kPorts; ++k) ports_[next++] = {p->id, k};
  }
  for (int i = 0; i < kPorts; ++i) {
    const Player* owner = ports_[i].player >= 0 ? player(ports_[i].player) : nullptr;
    paks_[i] = owner && (owner->rumble_mask >> ports_[i].local) & 1 ? 2 : 1;
  }
}

int Netplay::suggested_delay() const {
  // Enough frames to cover the slowest one-way trip, plus one for jitter.
  int worst = 0;
  for (const auto& p : players_) worst = std::max(worst, p.ping_ms);
  const int frames = static_cast<int>(std::ceil(worst / 2.0 / (1000.0 / 60.0))) + 1;
  return std::clamp(frames, kMinDelay, kMaxDelay);
}

void Netplay::set_delay(int frames) {
  if (!is_host() || phase_ != Phase::Lobby) return;
  delay_ = std::clamp(frames, kMinDelay, kMaxDelay);
  delay_chosen_ = true;
  broadcast_lobby();
}

void Netplay::broadcast_lobby() {
  if (!is_host()) return;
  if (!delay_chosen_) delay_ = suggested_delay();
  Writer w;
  w.u8(kLobby).u8(static_cast<unsigned>(delay_)).str(game_.id).str(game_.title).str(game_.internal_name).str(game_.region).bytes(game_.sha.data(), 32);
  w.u8(static_cast<unsigned>(players_.size()));
  for (const auto& p : players_) {
    w.u8(static_cast<unsigned>(p.id)).str(p.name).u16(static_cast<unsigned>(std::max(0, p.ping_ms)));
    w.u8(p.checked ? 1 : 0).str(p.problem).u8(static_cast<unsigned>(p.local_players));
  }
  for (const auto& port : ports_) w.u8(static_cast<unsigned>(port.player & 0xFF)).u8(static_cast<unsigned>(port.local));
  send(kBroadcast, w.b);
}

bool Netplay::can_start(std::string& why) const {
  if (!is_host() || phase_ != Phase::Lobby) {
    why = "Not ready";
    return false;
  }
  int guests = 0;
  for (const auto& p : players_) {
    if (p.id == 0) continue;
    ++guests;
    if (!p.checked) {
      why = "Waiting for " + p.name + " to check the game";
      return false;
    }
    if (!p.problem.empty()) {
      why = p.name + ": " + p.problem;
      return false;
    }
  }
  if (guests == 0) {
    why = "Waiting for a friend to join";
    return false;
  }
  return true;
}

void Netplay::start(const std::vector<uint8_t>& save) {
  std::string why;
  if (!can_start(why)) return;
  assign_ports();
  session_ = static_cast<uint32_t>(SDL_GetTicksNS() & 0xFFFFFFFF) | 1u;
  Writer w;
  w.u8(kStart).u32(session_).u8(static_cast<unsigned>(delay_));
  for (int i = 0; i < kPorts; ++i) {
    w.u8(static_cast<unsigned>(ports_[i].player & 0xFF)).u8(static_cast<unsigned>(ports_[i].local));
    w.u8(static_cast<unsigned>(paks_[i]));
  }
  w.str(game_.id).str(game_.title).str(game_.internal_name).str(game_.region).bytes(game_.sha.data(), 32);
  w.u32(static_cast<uint32_t>(save.size())).bytes(save.data(), save.size());
  send(kBroadcast, w.b);
  ws_.send(std::string{static_cast<char>(kToRelay), 1});  // the relay turns away anyone who arrives now
  for (auto& p : players_) p.ready = p.id == 0;
  phase_ = Phase::Starting;
  status_ = "Starting...";
}

void Netplay::report_game(bool ok, const std::string& problem) {
  if (is_host() || phase_ != Phase::Lobby) return;
  Writer w;
  w.u8(kHaveGame).u8(ok ? 1 : 0).str(ok ? std::string() : problem);
  send(0, w.b);
  status_ = ok ? "Ready. Waiting for the host to start" : problem;
}

void Netplay::report_ready() {
  if (is_host() || phase_ != Phase::Starting) return;
  Writer w;
  w.u8(kReady).u32(session_);
  send(0, w.b);
  status_ = "Ready";
}

void Netplay::begin() {
  phase_ = Phase::Running;
  status_.clear();
  ring_ = {};
  pending_.clear();
  my_hashes_.clear();
  their_hashes_.clear();
  frame_ = 0;
  reset_frame_ = UINT32_MAX;
  // The first `delay` frames have no input yet: everyone starts with nothing pressed.
  for (uint32_t f = 0; f < static_cast<uint32_t>(delay_); ++f) slot(f).known = 0xF;
  sampled_until_ = static_cast<uint32_t>(delay_);
  events_.push_back({Event::Begin});
}

// ---------------------------------------------------------------------------------------------
// Messages

void Netplay::on_binary(const std::string& data) {
  if (data.size() < 2) return;
  Reader r{data};
  const int from = static_cast<int>(r.u8());
  const unsigned type = r.u8();
  switch (type) {
    case kHello: {
      if (!is_host()) return;
      const unsigned proto = r.u16();
      const std::string versions = r.str();
      const int local = static_cast<int>(r.u8());
      const uint8_t rumble = static_cast<uint8_t>(r.u8());
      Player* p = player(from);
      if (!r.ok || !p) return;
      if (proto != kProtocol || versions != versions_) {
        Writer w;
        w.u8(kReject).str("Different versions of Prometheus 64. Host: " + versions_ + ", you: " + versions +
                          ". Install the same version on both computers");
        send(from, w.b);
        p->problem = "Has a different version of Prometheus 64";
        return;
      }
      p->local_players = std::clamp(local, 1, kPorts);
      p->rumble_mask = rumble;
      broadcast_lobby();
      return;
    }
    case kReject: {
      const std::string why = r.str();
      end(why, false);
      return;
    }
    case kLobby: {
      if (is_host() || from != 0) return;
      const int delay = static_cast<int>(r.u8());
      GameInfo g;
      g.id = r.str();
      g.title = r.str();
      g.internal_name = r.str();
      g.region = r.str();
      r.bytes(g.sha.data(), 32);
      const unsigned n = r.u8();
      std::vector<Player> list;
      for (unsigned i = 0; i < n && r.ok; ++i) {
        Player p;
        p.id = static_cast<int>(r.u8());
        p.name = r.str();
        p.ping_ms = static_cast<int>(r.u16());
        p.checked = r.u8() != 0;
        p.problem = r.str();
        p.local_players = static_cast<int>(r.u8());
        list.push_back(p);
      }
      std::array<Port, kPorts> ports{};
      for (auto& port : ports) {
        const unsigned owner = r.u8();
        port.player = owner == 0xFF ? -1 : static_cast<int>(owner);
        port.local = static_cast<int>(r.u8());
      }
      if (!r.ok) return;
      // Keep our own ping measurements; the host's list says who is in and how they are doing.
      for (auto& p : list) {
        if (const Player* mine = player(p.id); mine && p.id != 0 && p.id != me_) p.ping_ms = mine->ping_ms;
        if (p.id == 0) {
          const Player* host = player(0);
          p.ping_ms = host ? host->ping_ms : -1;
        }
      }
      players_ = list;
      ports_ = ports;
      delay_ = delay;
      const bool new_game = !game_checked_ || g.sha != game_.sha;
      game_ = g;
      if (new_game) {
        game_checked_ = true;
        status_ = "Checking that you have " + g.title + "...";
        events_.push_back({Event::CheckGame, {}, g});
      }
      return;
    }
    case kHaveGame: {
      if (!is_host()) return;
      const bool ok = r.u8() != 0;
      const std::string problem = r.str();
      if (Player* p = player(from); p && r.ok) {
        p->checked = true;
        p->problem = ok ? std::string() : problem;
        assign_ports();
        broadcast_lobby();
      }
      return;
    }
    case kStart: {
      if (is_host() || from != 0) return;
      session_ = r.u32();
      delay_ = static_cast<int>(r.u8());
      for (int i = 0; i < kPorts; ++i) {
        const unsigned owner = r.u8();
        ports_[i].player = owner == 0xFF ? -1 : static_cast<int>(owner);
        ports_[i].local = static_cast<int>(r.u8());
        paks_[i] = static_cast<int>(r.u8());
      }
      GameInfo g;
      g.id = r.str();
      g.title = r.str();
      g.internal_name = r.str();
      g.region = r.str();
      r.bytes(g.sha.data(), 32);
      const uint32_t n = r.u32();
      std::vector<uint8_t> save(n);
      if (!r.bytes(save.data(), n)) return;
      game_ = g;
      phase_ = Phase::Starting;
      status_ = "Loading " + g.title + "...";
      Event e{Event::LoadGame, {}, g};
      e.save = std::move(save);
      events_.push_back(std::move(e));
      return;
    }
    case kReady: {
      if (!is_host() || phase_ != Phase::Starting) return;
      if (r.u32() != session_) return;
      if (Player* p = player(from)) p->ready = true;
      const bool all = std::all_of(players_.begin(), players_.end(), [](const Player& p) { return p.ready; });
      if (all) {
        Writer w;
        w.u8(kGo).u32(session_);
        send(kBroadcast, w.b);
        begin();
      }
      return;
    }
    case kGo: {
      if (is_host() || from != 0 || phase_ != Phase::Starting) return;
      if (r.u32() != session_) return;
      begin();
      return;
    }
    case kInput: {
      if (phase_ != Phase::Running) return;
      const uint32_t first = r.u32();
      const unsigned frames = r.u8();
      const unsigned count = r.u8();
      std::vector<int> owned;
      for (int i = 0; i < kPorts; ++i) {
        if (ports_[i].player == from) owned.push_back(i);
      }
      if (count != owned.size()) return;
      for (unsigned f = 0; f < frames && r.ok; ++f) {
        Slot& s = slot(first + f);
        for (int port : owned) {
          s.pads[port] = get_pad(r);
          s.known |= static_cast<uint8_t>(1u << port);
        }
      }
      return;
    }
    case kHash: {
      const uint32_t frame = r.u32();
      const uint64_t h = r.u64();
      if (!r.ok) return;
      their_hashes_[frame][from] = h;
      auto mine = my_hashes_.find(frame);
      if (mine != my_hashes_.end() && mine->second != h && !desynced_) {
        desynced_ = true;
        events_.push_back({Event::Message, "Out of sync with " + player_name(from) +
                                               ". The game may play differently on each screen; restart the online game"});
      }
      return;
    }
    case kPing: {
      Writer w;
      w.u8(kPong).u64(r.u64());
      if (r.ok) send(from, w.b);
      return;
    }
    case kPong: {
      const uint64_t sent = r.u64();
      if (Player* p = player(from); p && r.ok) {
        p->ping_ms = static_cast<int>((SDL_GetTicksNS() - sent) / 1'000'000);
        if (is_host() && phase_ == Phase::Lobby) broadcast_lobby();
      }
      return;
    }
    case kReset: {
      if (from != 0) return;
      reset_frame_ = r.u32();
      return;
    }
    case kPause: {
      if (from != 0) return;
      host_paused_ = r.u8() != 0;
      return;
    }
    case kEnd: {
      if (from != 0) return;
      end(r.str(), false);
      return;
    }
    default:
      return;
  }
}

// ---------------------------------------------------------------------------------------------
// Lockstep

Netplay::Slot& Netplay::slot(uint32_t frame) {
  Slot& s = ring_[frame % ring_.size()];
  if (s.frame != frame) {
    s = {};
    s.frame = frame;
  }
  return s;
}

const Netplay::Slot* Netplay::find_slot(uint32_t frame) const {
  const Slot& s = ring_[frame % ring_.size()];
  return s.frame == frame ? &s : nullptr;
}

uint8_t Netplay::used_ports() const {
  uint8_t mask = 0;
  for (int i = 0; i < kPorts; ++i) {
    if (ports_[i].player >= 0) mask |= static_cast<uint8_t>(1u << i);
  }
  return mask;
}

void Netplay::sample_local(const std::vector<PadState>& local) {
  if (phase_ != Phase::Running) return;
  const uint32_t target = frame_ + static_cast<uint32_t>(delay_);
  if (sampled_until_ > target) return;  // already sampled for this frame
  std::vector<PadState> mine;
  Slot& s = slot(target);
  for (int i = 0; i < kPorts; ++i) {
    if (ports_[i].player != me_) continue;
    const int k = ports_[i].local;
    const PadState p = quantise(k < static_cast<int>(local.size()) ? local[k] : PadState{});
    s.pads[i] = p;
    s.known |= static_cast<uint8_t>(1u << i);
    mine.push_back(p);
  }
  sampled_until_ = target + 1;
  pending_.push_back({target, std::move(mine)});
}

void Netplay::flush(bool force) {
  if (pending_.empty() || (!force && static_cast<int>(pending_.size()) < kBatch)) return;
  Writer w;
  w.u8(kInput).u32(pending_.front().first).u8(static_cast<unsigned>(pending_.size()));
  w.u8(static_cast<unsigned>(pending_.front().second.size()));
  for (const auto& [frame, pads] : pending_) {
    for (const auto& p : pads) put_pad(w, p);
  }
  send(kBroadcast, w.b);
  pending_.clear();
}

bool Netplay::ready_to_run() const {
  if (phase_ != Phase::Running) return false;
  const Slot* s = find_slot(frame_);
  const uint8_t need = used_ports();
  return s && (s->known & need) == need;
}

PadState Netplay::input(int port) const {
  const Slot* s = find_slot(frame_);
  return s && port >= 0 && port < kPorts ? s->pads[port] : PadState{};
}

std::string Netplay::waiting_for() const {
  if (phase_ != Phase::Running || ready_to_run()) return {};
  const Slot* s = find_slot(frame_);
  std::string names;
  for (int i = 0; i < kPorts; ++i) {
    if (ports_[i].player < 0 || ports_[i].player == me_) continue;
    if (s && (s->known >> i) & 1) continue;
    const std::string n = player_name(ports_[i].player);
    if (names.find(n) == std::string::npos) names += (names.empty() ? "" : ", ") + n;
  }
  return names;
}

void Netplay::frame_done(const uint8_t* ram, std::size_t size) {
  if (phase_ != Phase::Running) return;
  if (reset_frame_ == frame_) reset_frame_ = UINT32_MAX;
  ++frame_;
  if (frame_ % kHashEvery == 0 && ram && size) {
    const uint64_t h = fingerprint(ram, size);
    my_hashes_[frame_] = h;
    Writer w;
    w.u8(kHash).u32(frame_).u64(h);
    send(kBroadcast, w.b);
    for (const auto& [id, theirs] : their_hashes_[frame_]) {
      if (theirs != h && !desynced_) {
        desynced_ = true;
        events_.push_back({Event::Message, "Out of sync with " + player_name(id) +
                                               ". The game may play differently on each screen; restart the online game"});
      }
    }
    // Keep only recent fingerprints.
    while (my_hashes_.size() > 8) my_hashes_.erase(my_hashes_.begin());
    while (their_hashes_.size() > 8) their_hashes_.erase(their_hashes_.begin());
  }
}

void Netplay::request_reset() {
  if (!is_host() || phase_ != Phase::Running) return;
  reset_frame_ = frame_ + static_cast<uint32_t>(delay_) + 2;  // far enough ahead for everyone to hear in time
  Writer w;
  w.u8(kReset).u32(reset_frame_);
  send(kBroadcast, w.b);
}

void Netplay::set_paused(bool paused) {
  if (!is_host() || phase_ != Phase::Running || host_paused_ == paused) return;
  host_paused_ = paused;
  Writer w;
  w.u8(kPause).u8(paused ? 1 : 0);
  send(kBroadcast, w.b);
}

}  // namespace p64
