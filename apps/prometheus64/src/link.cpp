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

// How many sent packets are kept for sending again (over a minute's worth), how long a broken
// connection is given to come back, and how long the relay may stay silent before the
// connection counts as dead (it answers a ping every 3 seconds).
constexpr std::size_t kKept = 4096;
constexpr uint64_t kComeBackNs = 45'000'000'000ull;
constexpr int kSilentMs = 9000;

void put32(std::string& out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}
uint32_t get32(const std::string& in, std::size_t at) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<uint8_t>(in[at + static_cast<std::size_t>(i)])) << (8 * i);
  return v;
}

}  // namespace

Link::~Link() { leave(); }

void Link::host(const std::string& relay_url, const std::string& key, const std::string& name, const std::string& mode,
                const std::string& game) {
  leave();
  name_ = name;
  mode_ = mode;
  game_ = game;
  relay_url_ = relay_url;
  key_ = key;
  hosting_ = true;
  open(url(false));
}

void Link::join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
                const std::string& mode, const std::string& game) {
  leave();
  name_ = name;
  mode_ = mode;
  game_ = game;
  relay_url_ = relay_url;
  key_ = key;
  hosting_ = false;
  code_ = code;
  open(url(false));
}

// The room's address: to open or join it, or (`again`) to come back to it after the connection
// broke, with what the relay needs to give this player its place again.
std::string Link::url(bool again) const {
  std::string u = relay_url_ + (hosting_ ? std::string("/v1/host") : "/v1/rooms/" + code_) + "?app=" + kApp +
                  "&v=" + std::to_string(kRelayProtocol) + "&key=" + net::url_encode(key_) + "&name=" + net::url_encode(name_);
  if (again) {
    if (hosting_) u += "&code=" + code_;
    else u += "&id=" + std::to_string(me_);
    if (!token_.empty()) u += "&resume=" + token_;
  }
  return u;
}

void Link::open(const std::string& url) {
  phase_ = Phase::Connecting;
  status_ = "Connecting...";
  me_ = -1;
  if (hosting_) code_.clear();
  peers_.clear();
  ws_.open(url);
}

void Link::leave() {
  // Leaving on purpose: say so first, so the others don't wait for this player to come back.
  if (phase_ == Phase::Open && !peers_.empty()) {
    std::string bye;
    bye += static_cast<char>(kBroadcast);
    bye += static_cast<char>(kBye);
    ws_.send(std::move(bye));
  }
  stop_core();
  ws_.close();
  phase_ = Phase::Off;
  me_ = -1;
  code_.clear();
  peers_.clear();
  lines_.clear();
  token_.clear();
  status_.clear();
  lost_ns_ = retry_ns_ = 0;
  reconnects_ = 0;
  resent_ = dropped_ = 0;
}

void Link::fail(const std::string& message) {
  SDL_Log("link: given up: %s", message.c_str());
  messages_.push_back(message);
  leave();
}

// The connection broke: the games stop where they are, and the room is connected to again.
void Link::begin_reconnect(const std::string& why) {
  if (phase_ != Phase::Open) return;
  SDL_Log("link: connection lost (%s); connecting again", why.c_str());
  phase_ = Phase::Reconnecting;
  status_ = "Reconnecting...";
  lost_ns_ = SDL_GetTicksNS();
  retry_ns_ = lost_ns_;
  for (auto& [id, peer] : peers_) {
    peer.away = true;
    lines_[id].since_ns = lost_ns_;
  }
  ws_.close();
}

// Tells a player the number of the last packet received from it, so it sends the rest again.
void Link::send_resume(int to) {
  Line& line = lines_[to];
  line.asked_ns = SDL_GetTicksNS();
  std::string msg;
  msg += static_cast<char>(to);
  msg += static_cast<char>(kResume);
  put32(msg, line.received);
  ws_.send(std::move(msg));
}

// A player is in the room again (or this app is): both say what they received, and wait for the
// other's word before the games go on.
void Link::peer_back(int id) {
  Peer& peer = peers_[id];
  Line& line = lines_[id];
  peer.away = false;
  line.synced = false;
  line.since_ns = SDL_GetTicksNS();
  say_hello(id);
  send_resume(id);
}

// A player is gone for good.
void Link::drop_peer(int id) {
  auto it = peers_.find(id);
  if (it == peers_.end()) return;
  messages_.push_back(it->second.name + " left the link");
  peers_.erase(it);
  pacers_.erase(id);
  lines_.erase(id);
  if (core_started_ && is_host() && core_ && core_->disconnected) core_->disconnected(static_cast<uint16_t>(id));
}

void Link::deliver(int from, const char* data, std::size_t len) {
  if (!core_started_ || !core_ || !core_->receive) return;
  ++g_received;
  count_core(data, len, false);
  core_->receive(data, len, static_cast<uint16_t>(from));
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
      case net::WebSocket::Event::Open:
        if (phase_ == Phase::Connecting) status_ = "Connected";
        break;
      case net::WebSocket::Event::Text: on_text(e.data); break;
      case net::WebSocket::Event::Binary: on_binary(e.data); break;
      case net::WebSocket::Event::Closed: on_closed(e); break;
    }
    if (phase_ == Phase::Off) return;          // the link ended
    if (phase_ == Phase::Reconnecting && !ws_.active()) break;  // the rest came from the connection that broke
  }
  if (phase_ == Phase::Open && core_started_ && core_ && core_->poll) core_->poll();
  if (phase_ == Phase::Off) return;
  const uint64_t now_ns = SDL_GetTicksNS();
  if (phase_ == Phase::Reconnecting) {
    if (now_ns - lost_ns_ > kComeBackNs) {
      fail("The connection was lost and didn't come back. Check your internet connection and start the link again");
      return;
    }
    if (!ws_.active() && now_ns >= retry_ns_) {
      retry_ns_ = now_ns + 1'500'000'000ull;
      ws_.open(url(true));
    }
    return;
  }
  if (phase_ == Phase::Open) {
    // The relay answers a ping every 3 seconds: silence means the connection is dead, whatever
    // the system says (a line can die without a word). Take it up again.
    if (const int silent = ws_.silent_ms(); silent > kSilentMs) {
      begin_reconnect("the relay has been silent for " + std::to_string(silent / 1000) + " s");
      return;
    }
    std::vector<int> gone;
    for (auto& [id, peer] : peers_) {
      Line& line = lines_[id];
      if (peer.away) {
        if (now_ns - line.since_ns > kComeBackNs) gone.push_back(id);
      } else if (!line.synced) {
        // Ask again every 2 seconds; a player that never answers is gone.
        if (now_ns - line.since_ns > kComeBackNs) gone.push_back(id);
        else if (now_ns - line.asked_ns > 2'000'000'000ull) send_resume(id);
      }
    }
    for (int id : gone) {
      SDL_Log("link: player %d didn't come back", id);
      if (id == 0 && !hosting_) {
        fail("The host's connection was lost and didn't come back. Join a new link when they're back");
        return;
      }
      drop_peer(id);
    }
  }
  // Every 2 seconds, the time to the others' apps and back (for the log).
  if (phase_ == Phase::Open && !peers_.empty()) {
    const uint64_t now = now_ns;
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
  out.relay_silent_ms = ws_.silent_ms();
  out.resent = resent_;
  out.dropped = dropped_;
  out.reconnects = reconnects_;
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
  if (t == "welcome" && phase_ == Phase::Reconnecting) {
    // Back in the room after a broken connection.
    const int you = static_cast<int>(v["you"].num());
    const std::string code = v["code"].str();
    const double secs = static_cast<double>(SDL_GetTicksNS() - lost_ns_) / 1e9;
    if (code != code_ && hosting_ && peers_.empty()) {
      // Nobody had joined yet, and this relay gave the room a new code (an older relay does).
      code_ = code;
      messages_.push_back("The connection dropped and is back. The link code is now " + code_);
    } else if (code != code_ || you != me_) {
      fail(hosting_ ? "The connection dropped, and this group's relay couldn't open the same link again. Host a new "
                      "link (whoever set the group up can update the relay: Play online, Update relay)"
                    : "The connection dropped and the link couldn't be taken up again. Join the link again");
      return;
    }
    token_ = v["token"].str();
    phase_ = Phase::Open;
    status_.clear();
    ++reconnects_;
    SDL_Log("link: connected again after %.1f s, as player %d, room %s", secs, me_, code_.c_str());
    if (!peers_.empty()) messages_.push_back("Reconnected: the link carries on");
    for (const auto& p : v["players"].array) {
      const int id = static_cast<int>(p["id"].num());
      if (id == me_ || !peers_.count(id)) continue;
      peer_back(id);
    }
  } else if (t == "welcome") {
    me_ = static_cast<int>(v["you"].num());
    code_ = v["code"].str();
    token_ = v["token"].str();
    phase_ = Phase::Open;
    status_.clear();
    if (token_.empty()) SDL_Log("link: this relay is an older one: it can't give a host its room again after a dropped connection");
    for (const auto& p : v["players"].array) {
      const int id = static_cast<int>(p["id"].num());
      if (id != me_) peers_[id].name = p["name"].str();
    }
    say_hello(kBroadcast);
    start_core();
  } else if (t == "join") {
    const int id = static_cast<int>(v["id"].num());
    if (auto it = peers_.find(id); it != peers_.end() && it->second.can_resume && core_started_) {
      // Someone whose connection broke is back (or the relay replaced their connection).
      SDL_Log("link: player %d is back", id);
      it->second.name = v["name"].str();
      peer_back(id);
      return;
    }
    peers_[id] = Peer{};
    peers_[id].name = v["name"].str();
    lines_[id] = Line{};
    messages_.push_back(peers_[id].name + " joined the link");
    say_hello(id);
    if (core_started_ && is_host() && core_ && core_->connected && !core_->connected(static_cast<uint16_t>(id))) {
      messages_.push_back("This game's link has no room for " + peers_[id].name);
    }
  } else if (t == "leave") {
    const int id = static_cast<int>(v["id"].num());
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    if (it->second.can_resume && !it->second.said_bye && core_started_) {
      // It didn't say goodbye: its connection broke. Wait for it to come back.
      SDL_Log("link: player %d's connection dropped; waiting for it to come back", id);
      it->second.away = true;
      lines_[id].since_ns = SDL_GetTicksNS();
      return;
    }
    drop_peer(id);
  }
}

void Link::on_closed(const net::WebSocket::Event& e) {
  if (phase_ == Phase::Reconnecting) {
    // An attempt failed. A relay that refuses for good ends it; anything else is tried again
    // (404: the host isn't back yet).
    if (e.status == 400 || e.status == 401 || e.status == 403 || e.status == 423) {
      fail("The link couldn't be taken up again: " + e.data);
      return;
    }
    SDL_Log("link: not back yet (%s)", e.data.c_str());
    retry_ns_ = SDL_GetTicksNS() + 1'500'000'000ull;
    return;
  }
  if (phase_ == Phase::Open && core_started_) {
    // The host closing the room sends everyone away (close code 4000). If it said goodbye, or
    // can't come back, that's the end; otherwise its connection broke and it will open the room
    // again.
    const auto host = peers_.find(0);
    const bool host_left = e.close_code == 4000;
    const bool final = host_left && (host == peers_.end() || host->second.said_bye || !host->second.can_resume);
    if (!final) {
      begin_reconnect(host_left ? "the host's connection dropped" : e.data);
      return;
    }
  } else if (phase_ == Phase::Open && hosting_ && peers_.empty() && e.close_code == 0) {
    begin_reconnect(e.data);  // waiting for a friend with no game running: keep the room open
    return;
  }
  messages_.push_back(e.data.empty() ? "The link ended" : "Link: " + e.data);
  leave();
}

Link::Wait Link::waiting_for() const {
  if (!core_started_) return Wait::None;
  if (phase_ == Phase::Reconnecting && !peers_.empty()) return Wait::Reconnect;
  for (const auto& [id, peer] : peers_) {
    if (peer.away) return Wait::Reconnect;
    if (const auto line = lines_.find(id); line != lines_.end() && !line->second.synced) return Wait::Reconnect;
  }
  for (const auto& [id, pacer] : pacers_) {
    if (pacer.behind(frames_)) return Wait::Pace;
  }
  return Wait::None;
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
    deliver(from, data.data() + 2, data.size() - 2);
  } else if (kind == kSeq) {
    // A numbered packet: passed on strictly in order. One already had is dropped; one that
    // comes too early (those before it were lost with a broken connection) is dropped too, and
    // the sender is asked to send from where this side stopped.
    if (data.size() < 6) return;
    Line& line = lines_[from];
    const uint32_t seq = get32(data, 2);
    if (seq == line.received + 1) {
      line.received = seq;
      deliver(from, data.data() + 6, data.size() - 6);
    } else if (seq <= line.received) {
      ++dropped_;
    } else if (SDL_GetTicksNS() - line.asked_ns > 1'000'000'000ull) {
      SDL_Log("link: packet %u from player %d came before %u: asking for them again", seq, from, line.received + 1);
      send_resume(from);
    }
  } else if (kind == kResume) {
    // The other side's count of what it received: send the rest again, in order.
    if (data.size() < 6) return;
    Line& line = lines_[from];
    const uint32_t have = get32(data, 2);
    if (have > line.sent || line.sent - have > line.kept.size()) {
      fail("The link couldn't be taken up again: too much was lost. Start the link again");
      return;
    }
    const uint32_t missing = line.sent - have;
    for (uint32_t i = 0; i < missing; ++i) {
      const uint32_t seq = have + 1 + i;
      std::string msg;
      msg += static_cast<char>(from);
      msg += static_cast<char>(kSeq);
      put32(msg, seq);
      msg += line.kept[line.kept.size() - missing + i];
      ws_.send(std::move(msg));
    }
    resent_ += missing;
    if (!line.synced || missing) SDL_Log("link: player %d has packets up to %u of %u: %u sent again", from, have, line.sent, missing);
    line.synced = true;
    if (auto it = peers_.find(from); it != peers_.end()) it->second.away = false;
  } else if (kind == kBye) {
    if (auto it = peers_.find(from); it != peers_.end()) it->second.said_bye = true;
  } else if (kind == kCaps) {
    if (auto it = peers_.find(from); it != peers_.end()) it->second.can_resume = true;
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
  // This app numbers its packets and can take a broken link up again.
  std::string caps;
  caps += static_cast<char>(to);
  caps += static_cast<char>(kCaps);
  caps += static_cast<char>(1);
  ws_.send(std::move(caps));
}

void RETRO_CALLCONV Link::send_cb(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  // Every packet is sent at once and in order (the relay connection is reliable), which is what
  // the core asks for with its flags.
  if (!g_running || !buf || !len) return;
  Link& l = *g_running;
  const std::string payload(static_cast<const char*>(buf), len);
  for (const auto& [id, peer] : l.peers_) {
    if (client_id != RETRO_NETPACKET_BROADCAST && id != client_id) continue;
    std::string msg;
    msg.reserve(len + 6);
    msg += static_cast<char>(id);
    if (peer.can_resume) {
      // Numbered and kept, so it can be sent again if the connection breaks before it arrives.
      Line& line = l.lines_[id];
      line.kept.push_back(payload);
      if (line.kept.size() > kKept) line.kept.pop_front();
      msg += static_cast<char>(kSeq);
      put32(msg, ++line.sent);
      msg += payload;
      if (l.phase_ == Phase::Open && !peer.away) l.ws_.send(std::move(msg));
    } else if (l.phase_ == Phase::Open) {
      msg += static_cast<char>(kCore);  // an app from before 0.2.13
      msg += payload;
      l.ws_.send(std::move(msg));
    }
  }
  ++g_sent;
  l.count_core(buf, len, true);
}

void RETRO_CALLCONV Link::poll_cb() {
  // The core can read mid-frame (waiting briefly for its partner); deliver what has arrived.
  if (!g_running) return;
  for (auto& e : g_running->ws_.poll()) {
    if (!g_running) return;  // the link ended while handling what arrived
    if (e.kind == net::WebSocket::Event::Binary) {
      g_running->on_binary(e.data);
    } else if (e.kind == net::WebSocket::Event::Text) {
      g_running->on_text(e.data);
    } else if (e.kind == net::WebSocket::Event::Closed) {
      g_running->on_closed(e);
      return;
    }
  }
}

}  // namespace p64
