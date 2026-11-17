#include "direct.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace p64::net {
namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kNoSocket = INVALID_SOCKET;
void close_socket(socket_t s) { closesocket(s); }
void winsock() {
  static const bool ready = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  (void)ready;
}
#else
using socket_t = int;
constexpr socket_t kNoSocket = -1;
void close_socket(socket_t s) { close(s); }
void winsock() {}
#endif

// Datagram: "PPD1", kind, the sender's token, the receiver's token, then the payload.
constexpr char kMagic[4] = {'P', 'P', 'D', '1'};
enum : uint8_t { kProbe = 1, kAck = 2, kData = 3, kPing = 4, kPong = 5 };
constexpr std::size_t kHeader = 4 + 1 + 8 + 8;
constexpr uint64_t kProbeEveryNs = 250'000'000ull;
constexpr uint64_t kPingEveryNs = 1'000'000'000ull;
constexpr uint64_t kDownAfterNs = 5'000'000'000ull;
constexpr uint64_t kStunEveryNs = 1'000'000'000ull;
constexpr int kStunTries = 8;
constexpr std::size_t kMaxCandidates = 6;
// Public STUN servers (they only tell a computer the address they see it at).
constexpr const char* kStunServers[][2] = {
    {"stun.cloudflare.com", "3478"}, {"stun.l.google.com", "19302"}, {"stun1.l.google.com", "19302"}};
constexpr uint32_t kStunCookie = 0x2112A442;

void put64(std::string& out, uint64_t v) {
  for (int i = 0; i < 8; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}
uint64_t get64(const char* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(static_cast<uint8_t>(p[i])) << (8 * i);
  return v;
}

uint64_t random64() {
  std::random_device rd;
  uint64_t v = 0;
  while (v == 0) v = (static_cast<uint64_t>(rd()) << 32) ^ rd();
  return v;
}

// An IPv4 or IPv6 address and port.
struct Addr {
  sockaddr_storage sa{};
  socklen_t len = 0;
  int family() const { return sa.ss_family; }
  const sockaddr* ptr() const { return reinterpret_cast<const sockaddr*>(&sa); }
};

std::string addr_text(const Addr& a) {
  char ip[INET6_ADDRSTRLEN] = {};
  if (a.family() == AF_INET6) {
    const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&a.sa);
    inet_ntop(AF_INET6, &v6->sin6_addr, ip, sizeof ip);
    return "[" + std::string(ip) + "]:" + std::to_string(ntohs(v6->sin6_port));
  }
  const auto* v4 = reinterpret_cast<const sockaddr_in*>(&a.sa);
  inet_ntop(AF_INET, &v4->sin_addr, ip, sizeof ip);
  return std::string(ip) + ":" + std::to_string(ntohs(v4->sin_port));
}

// "1.2.3.4:5678" or "[2001:db8::1]:5678".
bool parse_addr(const std::string& s, Addr& out) {
  const auto colon = s.rfind(':');
  if (colon == std::string::npos) return false;
  const int port = std::atoi(s.c_str() + colon + 1);
  if (port <= 0 || port > 65535) return false;
  out = {};
  if (s.size() > 2 && s[0] == '[' && colon > 1 && s[colon - 1] == ']') {
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&out.sa);
    v6->sin6_family = AF_INET6;
    v6->sin6_port = htons(static_cast<uint16_t>(port));
    out.len = sizeof(sockaddr_in6);
    return inet_pton(AF_INET6, s.substr(1, colon - 2).c_str(), &v6->sin6_addr) == 1;
  }
  auto* v4 = reinterpret_cast<sockaddr_in*>(&out.sa);
  v4->sin_family = AF_INET;
  v4->sin_port = htons(static_cast<uint16_t>(port));
  out.len = sizeof(sockaddr_in);
  return inet_pton(AF_INET, s.substr(0, colon).c_str(), &v4->sin_addr) == 1;
}

bool same(const Addr& a, const Addr& b) {
  return a.family() == b.family() && a.len == b.len && std::memcmp(&a.sa, &b.sa, a.len) == 0;
}

std::string hex64(uint64_t v) {
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(v));
  return buf;
}

// This computer's own address on its network (IPv4), or its internet address (IPv6, which needs no
// router in between): the one it would use to reach the internet (connecting a UDP socket sends
// nothing).
std::string own_address(int family) {
  const socket_t s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kNoSocket) return {};
  Addr to;
  parse_addr(family == AF_INET6 ? "[2001:4860:4860::8888]:53" : "8.8.8.8:53", to);
  std::string out;
  if (connect(s, to.ptr(), to.len) == 0) {
    Addr me;
    me.len = sizeof me.sa;
    if (getsockname(s, reinterpret_cast<sockaddr*>(&me.sa), &me.len) == 0) {
      const std::string text = addr_text(me);
      out = text.substr(0, text.rfind(':'));
      if (out == "0.0.0.0" || out == "[::]") out.clear();
      // Link-local and private IPv6 (fe80::, fd00::) can't be reached from outside.
      if (family == AF_INET6 && (out.rfind("[fe80", 0) == 0 || out.rfind("[fd", 0) == 0 || out.rfind("[fc", 0) == 0)) out.clear();
    }
  }
  close_socket(s);
  return out;
}

socket_t open_socket(int family, uint16_t port, uint16_t& bound_port) {
  const socket_t s = socket(family, SOCK_DGRAM, IPPROTO_UDP);
  if (s == kNoSocket) return s;
  Addr any;
  if (family == AF_INET6) {
    int only = 1;  // IPv6 only: IPv4 has its own socket
    setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&only), sizeof only);
    auto* v6 = reinterpret_cast<sockaddr_in6*>(&any.sa);
    v6->sin6_family = AF_INET6;  // any address: all zeros, as `any` starts
    v6->sin6_port = htons(port);
    any.len = sizeof(sockaddr_in6);
  } else {
    auto* v4 = reinterpret_cast<sockaddr_in*>(&any.sa);
    v4->sin_family = AF_INET;
    v4->sin_addr.s_addr = htonl(INADDR_ANY);
    v4->sin_port = htons(port);
    any.len = sizeof(sockaddr_in);
  }
  Addr bound;
  bound.len = sizeof bound.sa;
  if (bind(s, any.ptr(), any.len) != 0 || getsockname(s, reinterpret_cast<sockaddr*>(&bound.sa), &bound.len) != 0) {
    close_socket(s);
    return kNoSocket;
  }
#ifdef _WIN32
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);
#else
  fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
  const std::string text = addr_text(bound);
  bound_port = static_cast<uint16_t>(std::atoi(text.c_str() + text.rfind(':') + 1));
  return s;
}

}  // namespace

struct DirectPath::Impl {
  socket_t sock = kNoSocket;   // IPv4
  socket_t sock6 = kNoSocket;  // IPv6, when this computer has an internet address of that kind
  uint64_t token = 0;
  uint16_t port = 0;
  std::vector<std::string> local;  // "ip:port" this computer can be reached at without STUN
  std::string public_addr;         // as the internet sees it over IPv4 (STUN)
  bool changed = false;

  // STUN servers, looked up in the background (a name lookup can take seconds).
  std::mutex mutex;
  std::vector<Addr> stun;
  std::thread resolver;
  std::atomic<bool> resolved{false};
  uint8_t transaction[12] = {};
  uint64_t stun_ns = 0;
  int stun_sent = 0;

  struct Peer {
    uint64_t token = 0;
    std::vector<Addr> candidates;
    bool up = false;
    Addr addr;
    uint64_t heard_ns = 0, probe_ns = 0, ping_ns = 0;
    int rtt = -1;
  };
  std::map<int, Peer> peers;
  // Test mode (P64_DIRECT_MUTE="40-100"): from 40 to 100 seconds after start the path neither sends
  // nor hears anything, as if the routers had stopped letting it through.
  uint64_t started_ns = 0, mute_from_ns = 0, mute_to_ns = 0;
  bool muted(uint64_t now) const { return mute_to_ns && now - started_ns >= mute_from_ns && now - started_ns < mute_to_ns; }

  void datagram(const Addr& to, uint8_t kind, uint64_t their_token, const std::string& payload = {}) {
    if (muted(SDL_GetTicksNS())) return;
    const socket_t s = to.family() == AF_INET6 ? sock6 : sock;
    if (s == kNoSocket) return;
    std::string d(kMagic, 4);
    d += static_cast<char>(kind);
    put64(d, token);
    put64(d, their_token);
    d += payload;
    sendto(s, d.data(), static_cast<int>(d.size()), 0, to.ptr(), to.len);
  }
};

DirectPath::DirectPath() : impl_(std::make_unique<Impl>()) {}
DirectPath::~DirectPath() { stop(); }

bool DirectPath::start() {
  stop();
  winsock();
  Impl& d = *impl_;
  d.sock = open_socket(AF_INET, 0, d.port);
  if (d.sock == kNoSocket) return false;
  d.started_ns = SDL_GetTicksNS();
  d.mute_from_ns = d.mute_to_ns = 0;
  if (const char* mute = std::getenv("P64_DIRECT_MUTE")) {
    double a = 0, b = 0;
    if (std::sscanf(mute, "%lf-%lf", &a, &b) == 2 && b > a) {
      d.mute_from_ns = static_cast<uint64_t>(a * 1e9);
      d.mute_to_ns = static_cast<uint64_t>(b * 1e9);
    }
  }
  d.token = random64();
  d.local.clear();
  // IPv6 first: no router changes the address on the way, so it is the likeliest to get through.
  if (const std::string v6 = own_address(AF_INET6); !v6.empty()) {
    uint16_t port6 = 0;
    d.sock6 = open_socket(AF_INET6, d.port, port6);
    if (d.sock6 == kNoSocket) d.sock6 = open_socket(AF_INET6, 0, port6);
    if (d.sock6 != kNoSocket) d.local.push_back(v6 + ":" + std::to_string(port6));
  }
  if (const std::string lan = own_address(AF_INET); !lan.empty() && lan != "127.0.0.1") d.local.push_back(lan + ":" + std::to_string(d.port));
  d.local.push_back("127.0.0.1:" + std::to_string(d.port));  // two apps on one computer
  d.public_addr.clear();
  d.changed = true;
  d.stun_sent = 0;
  d.stun_ns = 0;
  std::random_device rd;
  for (auto& b : d.transaction) b = static_cast<uint8_t>(rd());
  d.resolved = false;
  d.resolver = std::thread([&d] {
    std::vector<Addr> found;
    for (const auto& server : kStunServers) {
      addrinfo hints{};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_DGRAM;
      addrinfo* res = nullptr;
      if (getaddrinfo(server[0], server[1], &hints, &res) == 0 && res) {
        Addr a;
        std::memcpy(&a.sa, res->ai_addr, res->ai_addrlen);
        a.len = static_cast<socklen_t>(res->ai_addrlen);
        found.push_back(a);
        freeaddrinfo(res);
      }
    }
    std::lock_guard<std::mutex> lock(d.mutex);
    d.stun = std::move(found);
    d.resolved = true;
  });
  std::string where;
  for (const auto& a : d.local) where += (where.empty() ? "" : ", ") + a;
  SDL_Log("direct path: listening (%s)", where.c_str());
  return true;
}

void DirectPath::stop() {
  Impl& d = *impl_;
  if (d.resolver.joinable()) d.resolver.join();
  if (d.sock != kNoSocket) close_socket(d.sock);
  if (d.sock6 != kNoSocket) close_socket(d.sock6);
  d.sock = d.sock6 = kNoSocket;
  d.peers.clear();
  d.stun.clear();
  d.public_addr.clear();
  d.local.clear();
}

bool DirectPath::running() const { return impl_->sock != kNoSocket; }

std::string DirectPath::offer() const {
  const Impl& d = *impl_;
  if (d.sock == kNoSocket) return {};
  std::string out = "v1;" + hex64(d.token);
  if (!d.public_addr.empty()) out += ";" + d.public_addr;
  for (const auto& a : d.local) out += ";" + a;
  return out;
}

bool DirectPath::offer_changed() {
  const bool c = impl_->changed;
  impl_->changed = false;
  return c && running();
}

void DirectPath::set_peer(int id, const std::string& offer) {
  Impl& d = *impl_;
  // "v1;<token>;<address>;<address>..."
  std::vector<std::string> parts;
  std::size_t at = 0;
  while (at <= offer.size()) {
    const auto semi = offer.find(';', at);
    parts.push_back(offer.substr(at, semi == std::string::npos ? std::string::npos : semi - at));
    if (semi == std::string::npos) break;
    at = semi + 1;
  }
  if (parts.size() < 3 || parts[0] != "v1") return;
  const uint64_t token = std::strtoull(parts[1].c_str(), nullptr, 16);
  if (!token) return;
  Impl::Peer& p = d.peers[id];
  if (p.token != token) p = Impl::Peer{};  // a new link from that player
  p.token = token;
  p.candidates.clear();
  for (std::size_t i = 2; i < parts.size() && p.candidates.size() < kMaxCandidates; ++i) {
    Addr a;
    if (parse_addr(parts[i], a) && (a.family() == AF_INET || d.sock6 != kNoSocket)) p.candidates.push_back(a);
  }
  p.probe_ns = 0;  // probe now
}

void DirectPath::remove_peer(int id) { impl_->peers.erase(id); }
void DirectPath::clear_peers() { impl_->peers.clear(); }

void DirectPath::update(std::vector<std::pair<int, std::string>>& received) {
  Impl& d = *impl_;
  if (d.sock == kNoSocket) return;
  const uint64_t now = SDL_GetTicksNS();

  // This computer's public IPv4 address, from STUN, asked from the socket the games will use (so the
  // router's door it opens is the one the other app needs).
  if (d.resolved && d.public_addr.empty() && d.stun_sent < kStunTries && now - d.stun_ns >= kStunEveryNs) {
    d.stun_ns = now;
    ++d.stun_sent;
    const std::string req = stun_request(d.transaction);
    std::lock_guard<std::mutex> lock(d.mutex);
    for (const auto& s : d.stun) sendto(d.sock, req.data(), static_cast<int>(req.size()), 0, s.ptr(), s.len);
  }

  char buf[2048];
  for (const socket_t sock : {d.sock, d.sock6}) {
    if (sock == kNoSocket) continue;
    for (int n = 0; n < 256; ++n) {
      Addr from;
      from.len = sizeof from.sa;
      const auto got = recvfrom(sock, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&from.sa), &from.len);
      if (got <= 0) break;
      if (d.muted(now)) continue;
      const std::string data(buf, static_cast<std::size_t>(got));
      if (data.size() >= 20 && static_cast<uint8_t>(data[0]) == 0x01 && static_cast<uint8_t>(data[1]) == 0x01) {
        const std::string mapped = stun_mapped_address(data, d.transaction);
        if (!mapped.empty() && mapped != d.public_addr) {
          d.public_addr = mapped;
          d.changed = true;
          SDL_Log("direct path: the internet sees this computer at %s", mapped.c_str());
        }
        continue;
      }
      if (data.size() < kHeader || std::memcmp(data.data(), kMagic, 4) != 0) continue;
      const uint8_t kind = static_cast<uint8_t>(data[4]);
      const uint64_t from_token = get64(data.data() + 5), to_token = get64(data.data() + 13);
      if (to_token != d.token) continue;
      auto it = std::find_if(d.peers.begin(), d.peers.end(), [&](const auto& p) { return p.second.token == from_token; });
      if (it == d.peers.end()) continue;
      Impl::Peer& p = it->second;
      p.heard_ns = now;
      if (!p.up || !same(p.addr, from)) {
        if (!p.up) SDL_Log("direct path: player %d reached directly at %s", it->first, addr_text(from).c_str());
        p.addr = from;
        p.up = true;
        p.ping_ns = 0;
      }
      if (kind == kProbe) {
        d.datagram(from, kAck, p.token);
      } else if (kind == kPing) {
        d.datagram(from, kPong, p.token, data.substr(kHeader));
      } else if (kind == kPong && data.size() >= kHeader + 8) {
        const uint64_t sent = get64(data.data() + kHeader);
        if (sent <= now) p.rtt = static_cast<int>((now - sent) / 1'000'000ull);
      } else if (kind == kData) {
        received.emplace_back(it->first, data.substr(kHeader));
      }
    }
  }

  for (auto& [id, p] : d.peers) {
    if (p.up && now > p.heard_ns && now - p.heard_ns > kDownAfterNs) {
      p.up = false;
      SDL_Log("direct path: player %d silent for %llu s: the relay carries the link alone", id,
              static_cast<unsigned long long>((now - p.heard_ns) / 1'000'000'000ull));
    }
    if (!p.up && now - p.probe_ns >= kProbeEveryNs) {
      p.probe_ns = now;
      for (const auto& c : p.candidates) d.datagram(c, kProbe, p.token);
    }
    if (p.up && now - p.ping_ns >= kPingEveryNs) {
      p.ping_ns = now;
      std::string ts;
      put64(ts, now);
      d.datagram(p.addr, kPing, p.token, ts);
    }
  }
}

bool DirectPath::up(int id) const {
  const auto it = impl_->peers.find(id);
  return it != impl_->peers.end() && it->second.up;
}

bool DirectPath::send(int id, const std::string& payload) {
  Impl& d = *impl_;
  const auto it = d.peers.find(id);
  if (d.sock == kNoSocket || it == d.peers.end() || !it->second.up || payload.size() > kMaxPayload) return false;
  d.datagram(it->second.addr, kData, it->second.token, payload);
  return true;
}

int DirectPath::rtt_ms(int id) const {
  const auto it = impl_->peers.find(id);
  return it == impl_->peers.end() ? -1 : it->second.rtt;
}

std::string DirectPath::describe(int id) const {
  const auto it = impl_->peers.find(id);
  if (!running()) return "off";
  if (it == impl_->peers.end()) return "no offer yet";
  const Impl::Peer& p = it->second;
  if (p.up) return "up" + (p.rtt >= 0 ? " " + std::to_string(p.rtt) + " ms" : std::string()) + " via " + addr_text(p.addr);
  return "trying " + std::to_string(p.candidates.size()) + " address" + (p.candidates.size() == 1 ? "" : "es");
}

std::string DirectPath::stun_request(const uint8_t transaction[12]) {
  std::string r;
  r += static_cast<char>(0x00);
  r += static_cast<char>(0x01);  // Binding request
  r += static_cast<char>(0x00);
  r += static_cast<char>(0x00);  // no attributes
  for (int i = 3; i >= 0; --i) r += static_cast<char>((kStunCookie >> (8 * i)) & 0xFF);
  r.append(reinterpret_cast<const char*>(transaction), 12);
  return r;
}

std::string DirectPath::stun_mapped_address(const std::string& response, const uint8_t transaction[12]) {
  const auto u8 = [&](std::size_t i) { return static_cast<uint8_t>(response[i]); };
  if (response.size() < 20 || u8(0) != 0x01 || u8(1) != 0x01) return {};  // Binding success response
  const uint32_t cookie = static_cast<uint32_t>(u8(4)) << 24 | u8(5) << 16 | u8(6) << 8 | u8(7);
  if (cookie != kStunCookie || std::memcmp(response.data() + 8, transaction, 12) != 0) return {};
  const std::size_t length = static_cast<std::size_t>(u8(2) << 8 | u8(3));
  std::size_t at = 20;
  std::string plain;
  while (at + 4 <= response.size() && at + 4 <= 20 + length) {
    const unsigned type = static_cast<unsigned>(u8(at) << 8 | u8(at + 1));
    const std::size_t len = static_cast<std::size_t>(u8(at + 2) << 8 | u8(at + 3));
    if (at + 4 + len > response.size()) break;
    // XOR-MAPPED-ADDRESS (0x0020), or the older MAPPED-ADDRESS (0x0001); IPv4 only.
    if ((type == 0x0020 || type == 0x0001) && len >= 8 && u8(at + 5) == 0x01) {
      uint16_t port = static_cast<uint16_t>(u8(at + 6) << 8 | u8(at + 7));
      uint32_t ip = static_cast<uint32_t>(u8(at + 8)) << 24 | u8(at + 9) << 16 | u8(at + 10) << 8 | u8(at + 11);
      if (type == 0x0020) {
        port ^= static_cast<uint16_t>(kStunCookie >> 16);
        ip ^= kStunCookie;
      }
      const std::string text = std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 255) + "." +
                               std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255) + ":" + std::to_string(port);
      if (type == 0x0020) return text;
      plain = text;
    }
    at += 4 + (len + 3) / 4 * 4;
  }
  return plain;
}

}  // namespace p64::net
