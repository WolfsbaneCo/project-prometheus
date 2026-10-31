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

 private:
  static void RETRO_CALLCONV send_first(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV send_second(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_first();
  static void RETRO_CALLCONV poll_second();
  void deliver(const retro_netpacket_callback* to, std::deque<std::vector<uint8_t>>& queue, uint16_t from);

  const retro_netpacket_callback* first_ = nullptr;
  const retro_netpacket_callback* second_ = nullptr;
  std::deque<std::vector<uint8_t>> to_first_, to_second_;
};

}  // namespace p64
