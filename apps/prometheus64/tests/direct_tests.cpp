// The direct path (src/direct.cpp): STUN's answer read, two paths on this computer finding each
// other from their offers and carrying data both ways, and a stranger's token refused.
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_timer.h>

#include "direct.hpp"
using p64::net::DirectPath;
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main() {
  // A Binding success response with XOR-MAPPED-ADDRESS 203.0.113.5:51234.
  uint8_t txn[12];
  for (int i = 0; i < 12; ++i) txn[i] = static_cast<uint8_t>(i * 7 + 1);
  const std::string req = DirectPath::stun_request(txn);
  CHECK(req.size() == 20 && req[1] == 1);
  std::string resp = {'\x01', '\x01', '\x00', '\x0C', '\x21', '\x12', '\xA4', '\x42'};
  resp.append(reinterpret_cast<const char*>(txn), 12);
  const uint16_t port = 51234 ^ 0x2112;
  const uint32_t ip = ((203u << 24) | (0u << 16) | (113u << 8) | 5u) ^ 0x2112A442u;
  resp += std::string{'\x00', '\x20', '\x00', '\x08', '\x00', '\x01'};
  resp += static_cast<char>(port >> 8);
  resp += static_cast<char>(port & 0xFF);
  for (int i = 3; i >= 0; --i) resp += static_cast<char>((ip >> (8 * i)) & 0xFF);
  CHECK(DirectPath::stun_mapped_address(resp, txn) == "203.0.113.5:51234");
  uint8_t other[12] = {};
  CHECK(DirectPath::stun_mapped_address(resp, other).empty());  // not our question

  // Two apps on this computer.
  DirectPath a, b;
  CHECK(a.start());
  CHECK(b.start());
  CHECK(!a.offer().empty());
  a.set_peer(1, b.offer());
  b.set_peer(0, a.offer());
  std::vector<std::pair<int, std::string>> ra, rb;
  for (int i = 0; i < 200 && !(a.up(1) && b.up(0)); ++i) {
    a.update(ra);
    b.update(rb);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(a.up(1));
  CHECK(b.up(0));
  CHECK(a.send(1, "hello b"));
  CHECK(b.send(0, "hello a"));
  CHECK(!a.send(1, std::string(DirectPath::kMaxPayload + 1, 'x')));  // too big for a datagram
  for (int i = 0; i < 100 && (ra.empty() || rb.empty()); ++i) {
    a.update(ra);
    b.update(rb);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(rb.size() == 1 && rb[0].first == 0 && rb[0].second == "hello b");
  CHECK(ra.size() == 1 && ra[0].first == 1 && ra[0].second == "hello a");

  // A third app that knows b's address but not its token is ignored.
  DirectPath c;
  CHECK(c.start());
  std::string fake = b.offer();
  fake.replace(3, 16, "0123456789abcdef");
  c.set_peer(1, fake);
  std::vector<std::pair<int, std::string>> rc;
  rb.clear();
  for (int i = 0; i < 60; ++i) {
    c.update(rc);
    b.update(rb);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  CHECK(!c.up(1));
  CHECK(rb.empty());

  std::printf("direct path tests: %s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails ? 1 : 0;
}
