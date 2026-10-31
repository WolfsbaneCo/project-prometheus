// The event gift station: the app plays the part of a Pokémon event's distribution machine, the
// way a real one did at a store or event (a GBA with a Wireless Adapter handing out Wonder Cards).
// The player's own game does everything a player did then: Mystery Gift > Wireless Communication,
// receiving the card, and later collecting the gift from the deliveryman in a Pokémon Center. The
// game saves the card itself; the app never edits the save or the game's memory.
//
// It talks to the game through gpSP's Wireless Adapter emulation (the libretro netpacket
// interface), in the app, with no network. Layer by layer, as the real machine did:
//   - gpSP's adapter messages (broadcast, connect, data): the station is the adapter's host;
//   - Nintendo's wireless library framing: "LL frames", each with a small header, every frame;
//     reliable transfers (NI: the game's name, the "you may join" reply) and the per-frame
//     unreliable one (UNI);
//   - Game Freak's link layer on top of UNI: each player's command words, echoed by the host to
//     everyone every frame, carrying player ids, blocks of data and "ready" signals;
//   - Mystery Gift: the server's script, the Wonder Card, the deliveryman's script.
// Written from the pret decompilations of Emerald (src/librfu_rfu.c, src/link_rfu_2.c,
// src/mystery_gift_*.c), which document what the games expect.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "gift_catalog.hpp"
#include "libretro.h"

namespace p64 {

class GiftStation {
 public:
  enum class Stage { Closed, Waiting, Connecting, Linked, Sending, Done, Failed };

  ~GiftStation();

  // The running core's netpacket interface (nullptr: none). Like Link::attach.
  void attach(const retro_netpacket_callback* core);
  // Opens the event with this gift; the station waits for the game to look for it.
  void open(const EventGift& gift);
  void close();
  bool is_open() const { return stage_ != Stage::Closed; }
  Stage stage() const { return stage_; }
  // What the player sees on the station's side ("Sending the card...").
  std::string status() const;
  // Once per frame, after the core ran.
  void update();
  // What happened, for tests and the app's log.
  const std::string& log() const { return log_; }

 private:
  using Cmd = std::array<uint16_t, 7>;  // one player's command words for a frame

  // Nintendo's reliable transfer (NI), one direction each.
  struct NiRecv {
    int state = 0;  // 0 idle, 1 control, 2 data, 3 last, 4 done
    std::array<uint8_t, 4> n{};
    std::array<std::size_t, 4> at{};
    int phase = 0;
    std::vector<uint8_t> control, data;
    std::size_t remain = 0, payload = 0;
  };
  struct NiSend {
    int state = 0;  // 0 idle, 1 control, 2 data, 3 last, 4 null frame, 5 done
    std::array<uint8_t, 4> n{};
    std::array<std::size_t, 4> at{};
    int phase = 0;
    std::vector<uint8_t> control, data;
    std::size_t remain = 0, payload = 0, control_at = 0;
  };
  // A block (Game Freak's SendBlock): sent or being received in 12-byte pieces.
  struct BlockIn {
    bool receiving = false;
    int count = 0;
    uint32_t got = 0;
    std::vector<uint8_t> data;
  };

  static void RETRO_CALLCONV send_cb(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_cb();
  void from_game(const uint8_t* data, std::size_t len);
  void deliver();
  void note(const std::string& line);
  void fail(const std::string& why);
  void reset_link();
  void broadcast();
  void send_msg(uint32_t type, uint32_t header, const uint8_t* data = nullptr, std::size_t len = 0);

  // Wireless library layer.
  void game_frames(const uint8_t* p, std::size_t n);
  void ni_frame(int state, int ack, int phase, int n, const uint8_t* data, std::size_t size);
  void ni_ack(int state, int phase, int n);
  void host_frame();
  void ni_send(const std::vector<uint8_t>& data, int sub_frame);

  // Link layer.
  void game_cmd(Cmd c);
  void queue_cmd(uint16_t c0, std::initializer_list<uint16_t> words = {});
  void queue_block(const std::vector<uint8_t>& data);
  void block_done(const std::vector<uint8_t>& data);
  std::vector<uint8_t> link_player_block() const;

  // Mystery Gift.
  void holder_visit(const std::vector<uint8_t>& game_data);
  void mg_step();
  void mg_send(uint16_t ident, std::vector<uint8_t> data);
  std::vector<uint8_t> client_script(std::initializer_list<std::pair<uint32_t, uint32_t>> cmds) const;

  const retro_netpacket_callback* core_ = nullptr;
  bool started_ = false;
  Stage stage_ = Stage::Closed;
  EventGift gift_;
  std::string log_, status_;
  std::deque<std::vector<uint8_t>> to_game_;
  uint64_t frame_ = 0;

  // Adapter
  bool connected_ = false;
  bool game_spoke_ = false;  // the game sent since the station's last frame
  uint64_t last_sent_ = 0;

  // Wireless library
  NiRecv ni_in_;
  NiSend ni_out_;
  bool uni_ = false;        // the per-frame exchange is running
  bool game_joined_ = false;

  // Link layer
  std::deque<Cmd> my_cmds_;
  Cmd echo_{};              // the game's last command, sent back once
  bool echo_pending_ = false;
  BlockIn block_in_;
  int link_step_ = 0;       // 0 waiting for the game, 1 ids sent, 2 blocks swapped
  int wait_ = 0;            // frames before the next step

  // Mystery Gift
  int mg_state_ = 0;
  std::vector<uint8_t> mg_in_;        // a transfer from the game being put together
  uint16_t mg_in_ident_ = 0, mg_in_size_ = 0, mg_in_crc_ = 0;
  bool mg_in_header_ = false;
  std::deque<std::vector<uint8_t>> mg_received_;  // complete transfers from the game
  std::vector<uint16_t> mg_received_ident_;
};

}  // namespace p64
