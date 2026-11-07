// The link log: while a GBA game is linked online, what each layer is doing, written to the log
// (prometheus.log) so a failed link can be traced afterwards from both players' logs.
//
// - As it happens: the game's own link layer changing state (connecting, connected, the number of
//   players, which game each linked player has), any link error the game records (checksum, a
//   full queue, lag, a hardware error), the menu opening and closing.
// - Every 5 seconds, one line: frames run and held, how far this game is ahead of the other, what
//   went over the link each way (and how much of it carried game data), the round trip to the
//   other player, the cable state on both sides (gpSP's), and the game's link queues.
//
// The game's link layer is read from its memory, where the pret decompilations place it, for the
// English Ruby, Sapphire, Emerald, FireRed and LeafGreen (gLink, gLinkStatus, gLinkPlayers). Read
// only: the app never writes to the game.
#pragma once

#include <cstdint>
#include <string>

#include "link.hpp"
#include "pokemon_hints.hpp"  // GbaMemory

namespace p64 {

class LinkMonitor {
 public:
  // Once per pass of the app's loop while a GBA game is linked. `held`: the game is waiting on
  // the other's (link_pacer.hpp); `menu`: the app's menu is open over it.
  void update(uint64_t now_ns, Link& link, const std::string& game_code, const GbaMemory& memory, bool held, bool menu);
  // The link ended: a last line, and the next link starts afresh.
  void stop(uint64_t now_ns, Link& link);
  bool active() const { return started_ns_ != 0; }

  // Where a game keeps its link layer (0: not known).
  struct Layout {
    uint32_t link, status, received, type, players, error_buffer;
  };

 private:
  struct Game {
    bool read = false;
    uint8_t master = 0, state = 0, id = 0, count = 0;
    uint8_t hardware = 0, checksum = 0, queue_full = 0, lag = 0;
    uint8_t send = 0, recv = 0;
    uint32_t status = 0;
    uint8_t received = 0;
    uint16_t type = 0;
    uint8_t error_buffer[8] = {};
  };
  Game read_game(const GbaMemory& memory) const;
  void log_changes(const Game& g, const GbaMemory& memory);
  void heartbeat(uint64_t now_ns, Link& link);

  const Layout* layout_ = nullptr;
  std::string code_;
  uint64_t started_ns_ = 0, window_ns_ = 0, last_ns_ = 0, held_ns_ = 0;
  uint64_t frames_at_window_ = 0;
  bool menu_ = false;
  Game last_;
  int send_max_ = 0, recv_max_ = 0;
  uint32_t pending_ = 0, logged_ = 0;  // the player count and status word: as seen, as last logged
  int pending_samples_ = 0;
};

}  // namespace p64
