// GBA link play: the link cable or the Wireless Adapter between two to four players' games, over
// the group's relay. Unlike N64 online play there is no lockstep: each player's game runs on its
// own computer at full speed, and only what goes over the link travels. The core (gpSP) speaks
// the link protocols itself and hands their messages over through the libretro netpacket
// interface; this class carries them between the players.
//
// Room ids come from the relay: the host is 0, the others 1 to 3, which is what the netpacket
// interface uses too. Every message starts with a byte saying what it is: a packet from the
// core, or "hello" (the player's name and link mode, so both ends can say when they differ).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"
#include "net.hpp"

namespace p64 {

class Link {
 public:
  enum class Phase { Off, Connecting, Open };
  struct Peer {
    std::string name, mode, game;
  };

  ~Link();

  // `mode` is this game's link mode ("cable" or "wireless"), `game` its title: shown to the others.
  void host(const std::string& relay_url, const std::string& key, const std::string& name, const std::string& mode,
            const std::string& game);
  void join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
            const std::string& mode, const std::string& game);
  void leave();

  // The running core's netpacket interface (nullptr: none, or no game). Set when a game starts and
  // cleared before it stops; the session starts in the core once both are there.
  void attach(const retro_netpacket_callback* core);

  // Once per frame, before the core runs: delivers what arrived, keeps the session going.
  void update();
  // Messages for the player ("Berna joined the link"); taken once.
  std::vector<std::string> take_messages();

  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Phase::Off; }
  bool is_host() const { return me_ == 0; }
  const std::string& code() const { return code_; }
  const std::string& status() const { return status_; }
  const std::map<int, Peer>& peers() const { return peers_; }
  std::string mode() const { return mode_; }

 private:
  enum Kind : uint8_t { kCore = 1, kHello = 2 };
  static constexpr uint8_t kBroadcast = 255;

  void open(const std::string& url);
  void on_text(const std::string& text);
  void on_binary(const std::string& data);
  void say_hello(int to);
  void start_core();
  void stop_core();

  // The core's callbacks have no user pointer: they go to the one link that is running.
  static void RETRO_CALLCONV send_cb(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_cb();

  net::WebSocket ws_;
  Phase phase_ = Phase::Off;
  int me_ = -1;
  std::string code_, status_, name_, mode_, game_;
  std::map<int, Peer> peers_;
  std::vector<std::string> messages_;
  const retro_netpacket_callback* core_ = nullptr;
  bool core_started_ = false;
};

}  // namespace p64
