#include "cable_station.hpp"

#include <cstring>

namespace p64 {

namespace {

CableStation* g_cable = nullptr;  // the station the core's callbacks go to
constexpr uint16_t kGameId = 0;     // netpacket ids: the game is the master Game Boy
constexpr uint16_t kStationId = 1;  // and the station the second
constexpr uint32_t kMagic = 0x4D504B31;  // "MPK1", gpSP's Pokémon cable messages
constexpr uint32_t kStateHandshake = 1, kStateConnected = 2;
constexpr uint16_t kMultiplayerId = 1;   // the station's place on the cable
constexpr uint16_t kLanguageEnglish = 2;

uint32_t get32be(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}
void put32be(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24), p[1] = static_cast<uint8_t>(v >> 16), p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

}  // namespace

CableStation::~CableStation() { close(); }

void CableStation::attach(const retro_netpacket_callback* core) {
  if (started_ && core_ && core_->stop) core_->stop();
  started_ = false;
  core_ = core;
  if (is_open() && core_) {
    g_cable = this;
    core_->start(kGameId, send_cb, poll_cb);
    started_ = true;
    reset();
  }
}

void CableStation::open(const EventGift& gift) {
  gift_ = gift;
  stage_ = Stage::Waiting;
  note("open: " + gift.name);
  attach(core_);
}

void CableStation::close() {
  if (started_ && core_ && core_->stop) core_->stop();
  started_ = false;
  stage_ = Stage::Closed;
  if (g_cable == this) g_cable = nullptr;
}

std::string CableStation::status() const {
  switch (stage_) {
    case Stage::Closed: return "Closed";
    case Stage::Waiting: return "Waiting for your game";
    case Stage::Linked: return "Linked";
    case Stage::Sending: return "Sending the event...";
    case Stage::Done: return "Event sent";
    case Stage::Failed: return "Failed";
  }
  return "";
}

void CableStation::note(const std::string& line) { log_ += line + "\n"; }

void CableStation::fail(const std::string& why) {
  note("failed: " + why);
  stage_ = Stage::Failed;
}

void CableStation::reset() {
  frame_ = heard_ = 0;
  game_state_ = 0;
  my_state_ = kStateHandshake;
  link_type_ = 0x5501;
  out_.clear();
  to_game_.clear();
  block_size_ = block_got_ = 0;
  got_players_ = sent_players_ = sent_event_ = closing_ = false;
  players_at_ = 0;
}

void CableStation::update() {
  if (!started_ || !core_ || stage_ == Stage::Closed) return;
  ++frame_;
  if (stage_ == Stage::Failed) {
    deliver();
    return;
  }
  // The cable's own handshake is gpSP's (the 0xB9A0 / 0x8FFF exchange): the station says it is
  // there and ready, and is connected once the game, as master, starts the link.
  if (my_state_ == kStateHandshake && game_state_ == kStateConnected) {
    my_state_ = kStateConnected;
    if (stage_ == Stage::Waiting) stage_ = Stage::Linked;
    note("the game opened the link");
  }
  // Done, the station still answers: its own close (0x5FFF) has to reach the game, which waits for
  // everyone's before it runs the event.
  // The event goes once the players are known and the game is waiting for it.
  if (got_players_ && sent_players_ && !sent_event_ && frame_ - players_at_ > 30) {
    queue_block(gift_.script);
    sent_event_ = true;
    stage_ = Stage::Sending;
    note("sending the event: " + std::to_string(gift_.script.size()) + " bytes");
  }
  if (my_state_ == kStateConnected && !out_.empty()) {
    send_state(true, out_.front());
    out_.pop_front();
  } else {
    send_state(false, Cmd{});
  }
  if (stage_ != Stage::Waiting && stage_ != Stage::Done && heard_ && frame_ - heard_ > 600) fail("the game stopped answering");
  deliver();
}

void CableStation::send_state(bool with_data, const Cmd& cmd) {
  std::vector<uint8_t> p(24, 0);
  put32be(&p[0], kMagic);
  put32be(&p[4], my_state_ | (with_data ? 0x80000000u : 0));
  for (std::size_t i = 0; i < 8; ++i) {
    p[8 + 2 * i] = static_cast<uint8_t>(cmd[i] >> 8);
    p[9 + 2 * i] = static_cast<uint8_t>(cmd[i]);
  }
  to_game_.push_back(std::move(p));
}

void CableStation::from_game(const uint8_t* data, std::size_t len) {
  if (len != 24 || get32be(data) != kMagic) return;
  const uint32_t flags = get32be(data + 4);
  game_state_ = flags & 0xFFFF;
  heard_ = frame_;
  if (!(flags & 0x80000000u)) return;
  Cmd c{};
  for (std::size_t i = 0; i < 8; ++i) c[i] = static_cast<uint16_t>(data[8 + 2 * i] << 8 | data[9 + 2 * i]);
  game_cmd(c);
}

// The game's command for one link frame (link.c, ProcessRecvCmds).
void CableStation::game_cmd(const Cmd& c) {
  switch (c[0]) {
    case 0x2222:  // the master asks for everyone's player data
      link_type_ = c[1];
      if (!sent_players_) {
        queue_block(link_player_block());
        sent_players_ = true;
        note("the game asked for player data (link type " + std::to_string(link_type_) + ")");
      }
      break;
    case 0xBBBB:  // a block from the game: its LinkPlayerBlock
      block_size_ = c[1];
      block_got_ = 0;
      break;
    case 0x8888:
      if (!block_size_) break;
      block_got_ += 14;
      if (block_got_ >= block_size_) {
        block_size_ = 0;
        if (!got_players_) {
          got_players_ = true;
          players_at_ = frame_;
          note("player data swapped");
        }
      }
      break;
    case 0x5FFF:  // closing the link: everyone says so
      if (!closing_) {
        closing_ = true;
        out_.push_back(Cmd{0x5FFF});
        if (sent_event_ && out_.size() == 1) {
          stage_ = Stage::Done;
          note("event sent; the link is closed");
        } else {
          fail("the game closed the link before the event arrived");
        }
      }
      break;
    default:
      break;
  }
}

// A block, as link.c sends one: its size, then 14 bytes a frame.
void CableStation::queue_block(const std::vector<uint8_t>& data) {
  out_.push_back(Cmd{0xBBBB, static_cast<uint16_t>(data.size()), static_cast<uint16_t>(kMultiplayerId + 128)});
  for (std::size_t at = 0; at < data.size(); at += 14) {
    Cmd c{0x8888};
    for (std::size_t i = 0; i < 7; ++i) {
      const std::size_t o = at + 2 * i;
      const uint16_t lo = o < data.size() ? data[o] : 0, hi = o + 1 < data.size() ? data[o + 1] : 0;
      c[i + 1] = static_cast<uint16_t>(hi << 8 | lo);
    }
    out_.push_back(c);
  }
}

// struct LinkPlayerBlock: "GameFreak inc." around a struct LinkPlayer (28 bytes), as the e-Reader
// sent: the same link type as the game, its language, no trainer.
std::vector<uint8_t> CableStation::link_player_block() const {
  std::vector<uint8_t> b(60, 0);
  const char magic[] = "GameFreak inc.";
  std::memcpy(&b[0], magic, sizeof magic);
  std::memcpy(&b[44], magic, sizeof magic);
  uint8_t* lp = &b[16];
  lp[0] = 0x00, lp[1] = 0x40;  // version: 0x4000 + game version (none)
  const std::vector<uint8_t> name = encode_game_text("EREADER");
  for (std::size_t i = 0; i < 11; ++i) lp[8 + i] = i < name.size() ? name[i] : 0xFF;
  lp[20] = static_cast<uint8_t>(link_type_), lp[21] = static_cast<uint8_t>(link_type_ >> 8);
  lp[24] = static_cast<uint8_t>(kMultiplayerId);
  lp[26] = static_cast<uint8_t>(kLanguageEnglish);
  return b;
}

void CableStation::deliver() {
  while (core_ && !to_game_.empty()) {
    std::vector<uint8_t> p = std::move(to_game_.front());
    to_game_.pop_front();
    core_->receive(p.data(), p.size(), kStationId);
  }
}

void RETRO_CALLCONV CableStation::send_cb(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  if (!g_cable || !buf) return;
  if (client_id != kStationId && client_id != 0xFFFF) return;
  g_cable->from_game(static_cast<const uint8_t*>(buf), len);
}

void RETRO_CALLCONV CableStation::poll_cb() {
  if (g_cable) g_cable->deliver();
}

}  // namespace p64
