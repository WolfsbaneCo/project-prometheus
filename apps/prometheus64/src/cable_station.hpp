// The e-Reader, for Ruby and Sapphire's Mystery Event: the app plays the second Game Boy on the
// link cable, as an e-Reader reading an event card did, and sends the game one event (the Eon
// Ticket). The player's own game does what a player did then: MYSTERY EVENT on the title menu,
// and later collecting the gift in the game. The app never edits the save or the game's memory.
//
// Layer by layer:
//   - gpSP's link cable emulation (the "MPK1" netpacket messages of its Pokémon cable mode): the
//     game is the master Game Boy (netpacket id 0), the station the second one (id 1). A message
//     carries a device's state (handshake, connected) and, when it has one, its 8 command words
//     for a link frame.
//   - Game Freak's link layer (link.c in the pret Ruby decompilation): each frame, each Game Boy
//     sends one command. The master asks for player data (0x2222); each side answers with its
//     LinkPlayerBlock as a block (0xBBBB: size; then 0x8888 frames of 14 bytes). Then the
//     station sends the event as a block, which the game keeps in its shared buffer, and both
//     sides close the link (0x5FFF).
//   - The Mystery Event script itself (gift_catalog.cpp), which the game checks and runs.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "gift_catalog.hpp"
#include "libretro.h"

namespace p64 {

class CableStation {
 public:
  enum class Stage { Closed, Waiting, Linked, Sending, Done, Failed };

  ~CableStation();

  void attach(const retro_netpacket_callback* core);
  // Opens the event (gift.script holds the event's data, gift.cable is set).
  void open(const EventGift& gift);
  void close();
  bool is_open() const { return stage_ != Stage::Closed; }
  Stage stage() const { return stage_; }
  std::string status() const;
  // Once per frame, after the core ran.
  void update();
  const std::string& log() const { return log_; }

 private:
  using Cmd = std::array<uint16_t, 8>;

  static void RETRO_CALLCONV send_cb(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_cb();
  void from_game(const uint8_t* data, std::size_t len);
  void deliver();
  void note(const std::string& line);
  void fail(const std::string& why);
  void reset();
  void send_state(bool with_data, const Cmd& cmd);
  void game_cmd(const Cmd& c);
  void queue_block(const std::vector<uint8_t>& data);
  std::vector<uint8_t> link_player_block() const;

  const retro_netpacket_callback* core_ = nullptr;
  bool started_ = false;
  Stage stage_ = Stage::Closed;
  EventGift gift_;
  std::string log_;
  std::deque<std::vector<uint8_t>> to_game_;
  uint64_t frame_ = 0;

  uint32_t game_state_ = 0;       // the game's cable state (0 none, 1 handshake, 2 connected)
  uint32_t my_state_ = 1;         // ours: handshake until the game connects
  uint32_t link_type_ = 0x5501;   // the game's link type, from its 0x2222 (Mystery Event: 0x5501)
  uint64_t heard_ = 0;            // frame_ when the game last sent anything
  std::deque<Cmd> out_;           // our commands, one per link frame
  // The game's block being received (its LinkPlayerBlock), and what has happened.
  uint32_t block_size_ = 0, block_got_ = 0;
  bool got_players_ = false, sent_players_ = false, sent_event_ = false, closing_ = false;
  uint64_t players_at_ = 0;       // frame_ when the link players were swapped
};

}  // namespace p64
