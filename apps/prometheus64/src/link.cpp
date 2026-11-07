#include "link.hpp"

#include <cstring>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include "json.hpp"

namespace p64 {
namespace {

constexpr const char* kApp = "prometheus-link";  // GBA link rooms on the group's relay (N64 games have their own)
constexpr int kRelayProtocol = 2;

Link* g_running = nullptr;  // the link whose session is running in the core
uint64_t g_sent = 0, g_received = 0;  // the core's packets, for the log

}  // namespace

Link::~Link() { leave(); }

void Link::host(const std::string& relay_url, const std::string& key, const std::string& name, const std::string& mode,
                const std::string& game) {
  leave();
  name_ = name;
  mode_ = mode;
  game_ = game;
  open(relay_url + "/v1/host?app=" + kApp + "&v=" + std::to_string(kRelayProtocol) + "&key=" + net::url_encode(key) +
       "&name=" + net::url_encode(name));
}

void Link::join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
                const std::string& mode, const std::string& game) {
  leave();
  name_ = name;
  mode_ = mode;
  game_ = game;
  open(relay_url + "/v1/rooms/" + code + "?app=" + kApp + "&v=" + std::to_string(kRelayProtocol) +
       "&key=" + net::url_encode(key) + "&name=" + net::url_encode(name));
}

void Link::open(const std::string& url) {
  phase_ = Phase::Connecting;
  status_ = "Connecting...";
  me_ = -1;
  code_.clear();
  peers_.clear();
  ws_.open(url);
}

void Link::leave() {
  stop_core();
  ws_.close();
  phase_ = Phase::Off;
  me_ = -1;
  code_.clear();
  peers_.clear();
  status_.clear();
}

void Link::attach(const retro_netpacket_callback* core) {
  if (core == core_) return;
  stop_core();
  core_ = core;
  start_core();
}

void Link::start_core() {
  if (!core_ || phase_ != Phase::Open || core_started_ || (g_running && g_running != this)) return;
  g_running = this;
  core_started_ = true;
  frames_ = 0;
  pacers_.clear();
  stats_ = Stats{};
  ping_sum_ = 0;
  SDL_Log("link: open as player %d%s, room %s, %s, game %s", me_, is_host() ? " (host)" : "", code_.c_str(), mode_.c_str(),
          game_.c_str());
  core_->start(static_cast<uint16_t>(me_), send_cb, poll_cb);
  // The host tells the core about everyone already in the room.
  if (is_host() && core_->connected) {
    for (const auto& [id, peer] : peers_) core_->connected(static_cast<uint16_t>(id));
  }
}

void Link::stop_core() {
  pacers_.clear();
  frames_ = 0;
  if (!core_started_) return;
  if (core_ && core_->stop) core_->stop();
  core_started_ = false;
  SDL_Log("link: closed after %llu packets sent, %llu received", static_cast<unsigned long long>(g_sent),
          static_cast<unsigned long long>(g_received));
  if (g_running == this) g_running = nullptr;
}

void Link::update() {
  if (phase_ == Phase::Off) return;
  for (auto& e : ws_.poll()) {
    switch (e.kind) {
      case net::WebSocket::Event::Open: status_ = "Connected"; break;
      case net::WebSocket::Event::Text: on_text(e.data); break;
      case net::WebSocket::Event::Binary: on_binary(e.data); break;
      case net::WebSocket::Event::Closed:
        messages_.push_back(e.data.empty() ? "The link ended" : "Link: " + e.data);
        leave();
        return;
    }
  }
  if (core_started_ && core_ && core_->poll) core_->poll();
  // Every 2 seconds, the time to the others' apps and back (for the log).
  if (phase_ == Phase::Open && !peers_.empty()) {
    const uint64_t now = SDL_GetTicksNS();
    if (now - last_ping_ns_ >= 2'000'000'000ull) {
      last_ping_ns_ = now;
      std::string msg;
      msg += static_cast<char>(kBroadcast);
      msg += static_cast<char>(kPing);
      for (int i = 0; i < 8; ++i) msg += static_cast<char>((now >> (8 * i)) & 0xFF);
      ws_.send(std::move(msg));
    }
  }
}

// The core's packets, counted for the log. In the Pokémon cable mode each is gpSP's MPK1 packet:
// "MPK1", then its cable state (0 starting, 1 handshake, 2 connected; top bit: game data follows),
// then 8 words of the game's data. A change of either side's state is logged as it happens.
void Link::count_core(const void* data, std::size_t len, bool sent) {
  const auto* p = static_cast<const uint8_t*>(data);
  (sent ? stats_.sent : stats_.received)++;
  (sent ? stats_.bytes_sent : stats_.bytes_received) += len;
  if (len != 24 || std::memcmp(p, "MPK1", 4) != 0) return;
  const int state = p[6] << 8 | p[7];
  if (p[4] & 0x80) (sent ? stats_.sent_data : stats_.received_data)++;
  int& last = sent ? stats_.my_cable : stats_.their_cable;
  if (state != last) {
    static const char* const kNames[] = {"starting", "handshake", "connected"};
    const auto name = [](int st) { return st >= 0 && st <= 2 ? kNames[st] : "?"; };
    SDL_Log("link: %s cable: %s -> %s", sent ? "this game's" : "the other game's", last < 0 ? "-" : name(last), name(state));
    last = state;
  }
}

Link::Stats Link::take_stats() {
  Stats out = stats_;
  out.frames = frames_;
  out.backlog = ws_.backlog();
  for (const auto& [id, pacer] : pacers_) {
    if (!pacer.known()) continue;
    const int64_t lead = pacer.lead(frames_);
    if (!out.paced || lead > out.lead) {
      out.lead = lead;
      out.their_frames = pacer.theirs();
    }
    out.paced = true;
  }
  if (out.pings) out.ping_avg = ping_sum_ / out.pings;
  // A new window: counts start again; the cable states carry on.
  const int mine = stats_.my_cable, theirs = stats_.their_cable;
  stats_ = Stats{};
  stats_.my_cable = mine;
  stats_.their_cable = theirs;
  ping_sum_ = 0;
  return out;
}

std::vector<std::string> Link::take_messages() {
  std::vector<std::string> out;
  out.swap(messages_);
  return out;
}

void Link::on_text(const std::string& text) {
  json::Value v;
  if (!json::parse(text, v)) return;
  const std::string t = v["t"].str();
  if (t == "welcome") {
    me_ = static_cast<int>(v["you"].num());
    code_ = v["code"].str();
    phase_ = Phase::Open;
    status_.clear();
    for (const auto& p : v["players"].array) {
      const int id = static_cast<int>(p["id"].num());
      if (id != me_) peers_[id].name = p["name"].str();
    }
    say_hello(kBroadcast);
    start_core();
  } else if (t == "join") {
    const int id = static_cast<int>(v["id"].num());
    peers_[id].name = v["name"].str();
    messages_.push_back(peers_[id].name + " joined the link");
    say_hello(id);
    if (core_started_ && is_host() && core_ && core_->connected && !core_->connected(static_cast<uint16_t>(id))) {
      messages_.push_back("This game's link has no room for " + peers_[id].name);
    }
  } else if (t == "leave") {
    const int id = static_cast<int>(v["id"].num());
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    messages_.push_back(it->second.name + " left the link");
    peers_.erase(it);
    pacers_.erase(id);
    if (core_started_ && is_host() && core_ && core_->disconnected) core_->disconnected(static_cast<uint16_t>(id));
  }
}

// What arrives: [sender][kind][payload], the relay having put the sender's id in front.
void Link::on_binary(const std::string& data) {
  if (data.size() < 2) return;
  const int from = static_cast<uint8_t>(data[0]);
  const uint8_t kind = static_cast<uint8_t>(data[1]);
  if (kind == kCore) {
    // A partner whose hello came without a version runs an app from before 0.2.9.
    if (auto it = peers_.find(from); it != peers_.end() && !it->second.mode.empty() && it->second.version.empty() &&
                                     !it->second.version_told && !version_.empty()) {
      it->second.version_told = true;
      messages_.push_back(it->second.name + " has an older Project Prometheus than this one (" + version_ +
                          "). If the link breaks, both of you need the same version (Releases page)");
    }
    if (core_started_ && core_ && core_->receive) {
      ++g_received;
      count_core(data.data() + 2, data.size() - 2, false);
      core_->receive(data.data() + 2, data.size() - 2, static_cast<uint16_t>(from));
    }
  } else if (kind == kTick) {
    if (data.size() >= 10 && core_started_) {
      uint64_t frames = 0;
      for (int i = 0; i < 8; ++i) frames |= static_cast<uint64_t>(static_cast<uint8_t>(data[2 + i])) << (8 * i);
      LinkPacer& pacer = pacers_[from];
      const bool restarted = pacer.known() && frames < pacer.theirs();
      const bool first = !pacer.known();
      pacer.heard(frames, frames_);
      if (first || restarted)
        SDL_Log("link: pacing with player %d%s: their frame %llu, ours %llu, starting gap %lld frames", from,
                restarted ? " (their link started again)" : "", static_cast<unsigned long long>(frames),
                static_cast<unsigned long long>(frames_), static_cast<long long>(pacer.base()));
    }
  } else if (kind == kPing) {
    if (data.size() >= 10 && from != me_) {
      std::string msg = data.substr(2, 8);
      msg.insert(0, 1, static_cast<char>(kPong));
      msg.insert(0, 1, static_cast<char>(from));
      ws_.send(std::move(msg));
    }
  } else if (kind == kPong) {
    if (data.size() >= 10) {
      uint64_t sent = 0;
      for (int i = 0; i < 8; ++i) sent |= static_cast<uint64_t>(static_cast<uint8_t>(data[2 + i])) << (8 * i);
      const uint64_t now = SDL_GetTicksNS();
      if (sent <= now && now - sent < 60'000'000'000ull) {
        const double ms = static_cast<double>(now - sent) / 1e6;
        if (!stats_.pings || ms < stats_.ping_min) stats_.ping_min = ms;
        if (!stats_.pings || ms > stats_.ping_max) stats_.ping_max = ms;
        ping_sum_ += ms;
        ++stats_.pings;
      }
    }
  } else if (kind == kVersion) {
    Peer& p = peers_[from];
    p.version = data.substr(2);
    SDL_Log("link: player %d has Project Prometheus %s", from, p.version.c_str());
    if (!p.version_told && !version_.empty() && p.version != version_) {
      p.version_told = true;
      messages_.push_back((p.name.empty() ? std::string("Your friend") : p.name) + " has Project Prometheus " + p.version +
                          ", this computer " + version_ + ": the link needs the same version on both (Releases page)");
    }
  } else if (kind == kHello) {
    // mode \n game \n name
    const std::string body = data.substr(2);
    const auto a = body.find('\n'), b = body.find('\n', a == std::string::npos ? a : a + 1);
    if (a == std::string::npos || b == std::string::npos) return;
    Peer& p = peers_[from];
    const bool known = !p.mode.empty();
    p.mode = body.substr(0, a);
    p.game = body.substr(a + 1, b - a - 1);
    p.name = body.substr(b + 1);
    if (!known) SDL_Log("link: player %d is %s, playing %s, %s", from, p.name.c_str(), p.game.c_str(), p.mode.c_str());
    if (!known && p.mode != mode_) {
      messages_.push_back(p.name + " uses the " + (p.mode == "wireless" ? "Wireless Adapter" : "link cable") +
                          ", this game the " + (mode_ == "wireless" ? "Wireless Adapter" : "link cable") +
                          ": both need the same (Play online > Link)");
    }
  }
}

void Link::frame_done() {
  if (!core_started_) return;
  ++frames_;
  if (frames_ % LinkPacer::kEvery) return;
  std::string msg;
  msg += static_cast<char>(kBroadcast);
  msg += static_cast<char>(kTick);
  for (int i = 0; i < 8; ++i) msg += static_cast<char>((frames_ >> (8 * i)) & 0xFF);
  ws_.send(std::move(msg));
}

bool Link::waiting() const {
  if (!core_started_) return false;
  for (const auto& [id, pacer] : pacers_) {
    if (pacer.behind(frames_)) return true;
  }
  return false;
}

void Link::give_up_waiting() {
  for (auto& [id, pacer] : pacers_) pacer.give_up();
}

void Link::say_hello(int to) {
  std::string msg;
  msg += static_cast<char>(to);
  msg += static_cast<char>(kHello);
  msg += mode_ + "\n" + game_ + "\n" + name_;
  ws_.send(std::move(msg));
  if (!version_.empty()) {
    std::string v;
    v += static_cast<char>(to);
    v += static_cast<char>(kVersion);
    v += version_;
    ws_.send(std::move(v));
  }
}

void RETRO_CALLCONV Link::send_cb(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  // Every packet is sent at once and in order (the relay connection is reliable), which is what
  // the core asks for with its flags.
  if (!g_running || !buf || !len) return;
  std::string msg;
  msg.reserve(len + 2);
  msg += static_cast<char>(client_id == RETRO_NETPACKET_BROADCAST ? kBroadcast : static_cast<uint8_t>(client_id));
  msg += static_cast<char>(kCore);
  msg.append(static_cast<const char*>(buf), len);
  g_running->ws_.send(std::move(msg));
  ++g_sent;
  g_running->count_core(buf, len, true);
}

void RETRO_CALLCONV Link::poll_cb() {
  // The core can read mid-frame (waiting briefly for its partner); deliver what has arrived.
  if (!g_running) return;
  for (auto& e : g_running->ws_.poll()) {
    if (e.kind == net::WebSocket::Event::Binary) {
      g_running->on_binary(e.data);
    } else if (e.kind == net::WebSocket::Event::Text) {
      g_running->on_text(e.data);
    } else if (e.kind == net::WebSocket::Event::Closed) {
      g_running->messages_.push_back(e.data.empty() ? "The link ended" : "Link: " + e.data);
      g_running->leave();
      return;
    }
  }
}

}  // namespace p64
