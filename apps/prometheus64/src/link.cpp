#include "link.hpp"

#include <SDL3/SDL_log.h>

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
  SDL_Log("link: open as player %d%s", me_, is_host() ? " (host)" : "");
  core_->start(static_cast<uint16_t>(me_), send_cb, poll_cb);
  // The host tells the core about everyone already in the room.
  if (is_host() && core_->connected) {
    for (const auto& [id, peer] : peers_) core_->connected(static_cast<uint16_t>(id));
  }
}

void Link::stop_core() {
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
    if (core_started_ && is_host() && core_ && core_->disconnected) core_->disconnected(static_cast<uint16_t>(id));
  }
}

// What arrives: [sender][kind][payload], the relay having put the sender's id in front.
void Link::on_binary(const std::string& data) {
  if (data.size() < 2) return;
  const int from = static_cast<uint8_t>(data[0]);
  const uint8_t kind = static_cast<uint8_t>(data[1]);
  if (kind == kCore) {
    if (core_started_ && core_ && core_->receive) {
      ++g_received;
      core_->receive(data.data() + 2, data.size() - 2, static_cast<uint16_t>(from));
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
    if (!known && p.mode != mode_) {
      messages_.push_back(p.name + " uses the " + (p.mode == "wireless" ? "Wireless Adapter" : "link cable") +
                          ", this game the " + (mode_ == "wireless" ? "Wireless Adapter" : "link cable") +
                          ": both need the same (Play online > Link)");
    }
  }
}

void Link::say_hello(int to) {
  std::string msg;
  msg += static_cast<char>(to);
  msg += static_cast<char>(kHello);
  msg += mode_ + "\n" + game_ + "\n" + name_;
  ws_.send(std::move(msg));
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
