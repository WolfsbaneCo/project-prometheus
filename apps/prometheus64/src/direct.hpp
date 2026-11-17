// A direct path between two players' apps, beside the relay: UDP datagrams straight from one
// computer to the other, without going through Cloudflare. Online play sends what matters over both
// the relay and this path and takes whichever copy arrives first, so either can fail without the
// game stopping (link.hpp, netplay.hpp).
//
// How the two computers find each other: each opens a UDP socket and learns the address the
// internet sees it at from public STUN servers (Cloudflare's and Google's), then tells the other,
// through the relay, every address it might be reached at (that one, and its own on the home
// network). Both then send probes to all of the other's addresses at once; the probes open each
// home router's door for the other's replies ("hole punching"), and the first probe to get through
// in either direction makes the path. That works through most home routers; some mobile and shared
// connections never let it through, and then the relay alone carries the game, as before.
//
// Every datagram carries both apps' tokens (random, new for each link, exchanged over the relay,
// which the group's key protects), so nothing else on the internet can talk into a link.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace p64::net {

class DirectPath {
 public:
  DirectPath();
  ~DirectPath();
  DirectPath(const DirectPath&) = delete;
  DirectPath& operator=(const DirectPath&) = delete;

  // Opens the socket and starts looking up this computer's public address. Returns false when no
  // socket could be opened (the relay alone carries the link then).
  bool start();
  void stop();
  bool running() const;

  // This computer's offer for the others: its token and every address it may be reached at. It
  // grows when the public address becomes known: send it again whenever offer_changed() says so.
  std::string offer() const;
  bool offer_changed();
  // Another player's offer (from the relay). Probing starts at once.
  void set_peer(int id, const std::string& offer);
  void remove_peer(int id);
  void clear_peers();

  // Every frame: sends probes and keep-alives, reads what arrived. `received` gets each datagram's
  // payload with the sender's player id.
  void update(std::vector<std::pair<int, std::string>>& received);
  // The path to this player works now (heard from it within the last few seconds).
  bool up(int id) const;
  // Sends a payload straight to a player. False when the path isn't up or the payload is too big
  // for one datagram (the relay carries it then).
  bool send(int id, const std::string& payload);
  // The last measured round trip to the player over this path, in milliseconds (-1: none yet).
  int rtt_ms(int id) const;
  // For the log: "up 182 ms via 203.0.113.5:51234", "trying 3 addresses", "no offer yet".
  std::string describe(int id) const;
  static constexpr std::size_t kMaxPayload = 1200;

  // STUN (RFC 5389), exposed for the tests: a Binding request, and the mapped address in a
  // response ("1.2.3.4:5678"; empty if it isn't a matching response).
  static std::string stun_request(const uint8_t transaction[12]);
  static std::string stun_mapped_address(const std::string& response, const uint8_t transaction[12]);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace p64::net
