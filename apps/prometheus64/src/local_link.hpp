// Local link play: two of the player's own GBA games on this computer, joined as if by a link cable
// or the Wireless Adapter (to trade between them, battle, mix records). Each game runs in its own
// copy of the core (gpSP holds its state in globals, so the second game needs a second copy of the
// library, loaded from its own file). The cores speak the link protocols themselves, as for link
// play over the relay; this class hands each one's link messages straight to the other, in memory.
// The first game is the host (netpacket id 0), the second the guest (id 1).
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "libretro.h"

namespace p64 {

class LocalLink {
 public:
  ~LocalLink();

  // Joins the two games' link ports. Both cores must have a game loaded, in the same link mode.
  void start(const retro_netpacket_callback* first, const retro_netpacket_callback* second);
  void stop();
  bool active() const { return first_ != nullptr; }

  // Before each game runs a frame: hands it what the other game sent.
  void before_first();
  void before_second();

  // For the test bench: hold each message back as a connection between two distant computers
  // would, `frames` frames one way plus up to `jitter` more (messages stay in order, as over the
  // relay's connection). The app links with none. `tick` marks each frame of both games.
  void set_delay(int frames, int jitter) { delay_ = frames; jitter_ = jitter; }
  void tick() { ++now_; }

 private:
  static void RETRO_CALLCONV send_first(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV send_second(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_first();
  static void RETRO_CALLCONV poll_second();
  struct Message {
    uint64_t due;  // the frame it arrives in
    std::vector<uint8_t> data;
  };
  void deliver(const retro_netpacket_callback* to, std::deque<Message>& queue, uint16_t from);
  void queue(std::deque<Message>& q, const void* buf, size_t len);

  const retro_netpacket_callback* first_ = nullptr;
  const retro_netpacket_callback* second_ = nullptr;
  std::deque<Message> to_first_, to_second_;
  int delay_ = 0, jitter_ = 0;
  uint64_t now_ = 0, rng_ = 0x9E3779B97F4A7C15ull;
};

}  // namespace p64
