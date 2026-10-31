#include "gift_station.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace p64 {
namespace {

GiftStation* g_station = nullptr;  // the station the core's callbacks go to
constexpr uint16_t kStationId = 0;  // netpacket ids: the station hosts, the game is 1
constexpr uint16_t kGameId = 1;

// gpSP's adapter messages ("RFU1"): a 12-byte header of big-endian words (magic, type, a header
// word), then the payload.
constexpr uint32_t kRfuMagic = 0x52465531;
enum RfuMsg : uint32_t {
  kBroadcast = 0x00,
  kConnectReq = 0x01,
  kConnectAck = 0x02,
  kDisconnect = 0x04,
  kHostSend = 0x05,
  kClientSend = 0x06,
  kClientAck = 0x07,
};

constexpr uint16_t kHostDevice = 0x5150;  // the adapter id the station broadcasts with
constexpr uint16_t kGameDevice = 0x2A6D;  // the id it gives the game's adapter
constexpr int kBroadcastEvery = 30;       // frames, as gpSP's own hosts do

// Nintendo's wireless library: what a frame's header says about it.
enum : int { kNull = 0, kNiStart = 1, kNi = 2, kNiEnd = 3, kUni = 4 };

// Game Freak's link commands (the high byte of a player's first command word).
enum : uint16_t {
  kCmdReadyCloseLink = 0x5F00,
  kCmdReadyExitStandby = 0x6600,
  kCmdPlayerIds = 0x7700,
  kCmdBlockInit = 0x8800,
  kCmdBlock = 0x8900,
  kCmdBlockReq = 0xA100,
};
constexpr uint8_t kJoinGroupOk = 5;  // the host's answer to a game that asked to join

// Mystery Gift transfer ids and the client's script commands.
enum : uint16_t {
  kMgClientScript = 16,
  kMgGameData = 17,
  kMgResponse = 19,
  kMgReadyEnd = 20,
  kMgDynamicMsg = 21,
  kMgCard = 22,
  kMgStamp = 24,
  kMgRamScript = 25,
  kMgEReaderTrainer = 26,
};
enum : uint32_t {
  kCliReturn = 1,
  kCliRecv = 2,
  kCliSendLoaded = 3,
  kCliCopyRecv = 4,
  kCliLoadGameData = 8,
  kCliSaveCard = 10,
  kCliCopyMsg = 12,
  kCliAskToss = 13,
  kCliLoadTossResponse = 14,
  kCliSaveStamp = 16,
  kCliSaveRamScript = 17,
  kCliRecvEReaderTrainer = 18,
  kCliSendReadyEnd = 20,
};
enum : uint32_t {  // what the game says at the end
  kCliMsgCardReceived = 2,
  kCliMsgStampReceived = 4,
  kCliMsgHadCard = 5,
  kCliMsgHadStamp = 6,
  kCliMsgNoRoomStamps = 8,
  kCliMsgCantAccept = 10,
  kCliMsgTrainerReceived = 12,
  kCliMsgBufferFailure = 14,
};
// struct MysteryGiftLinkGameData (pokeemerald include/mystery_gift.h): what the game sends. The card's
// metadata starts word-aligned, at 32 (checked against the game's own data).
constexpr std::size_t kGameDataCardFlag = 20, kGameDataWords = 22, kGameDataStampSpecies = 40, kGameDataStampIds = 54,
                      kGameDataMaxStamps = 68;

void put32be(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

uint32_t get32be(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) << 24 | static_cast<uint32_t>(p[1]) << 16 | static_cast<uint32_t>(p[2]) << 8 | p[3];
}

void put16(std::vector<uint8_t>& out, uint16_t v) {
  out.push_back(static_cast<uint8_t>(v));
  out.push_back(static_cast<uint8_t>(v >> 8));
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

}  // namespace

GiftStation::~GiftStation() { close(); }

void GiftStation::attach(const retro_netpacket_callback* core) {
  if (started_ && core_ && core_->stop) core_->stop();
  started_ = false;
  core_ = core;
  if (is_open() && core_) {
    g_station = this;
    core_->start(kGameId, send_cb, poll_cb);
    started_ = true;
    reset_link();
  }
}

void GiftStation::open(const EventGift& gift) {
  gift_ = gift;
  stage_ = Stage::Waiting;
  note("open: " + gift.name);
  attach(core_);
}

void GiftStation::close() {
  if (started_ && core_ && core_->stop) core_->stop();
  started_ = false;
  stage_ = Stage::Closed;
  if (g_station == this) g_station = nullptr;
}

std::string GiftStation::status() const {
  if (!status_.empty()) return status_;
  switch (stage_) {
    case Stage::Closed: return "";
    case Stage::Waiting: return "Waiting for your game";
    case Stage::Connecting: return "Connecting...";
    case Stage::Linked: return "Linked";
    case Stage::Sending: return "Sending the gift...";
    case Stage::Done: return "Done";
    case Stage::Failed: return "The link failed";
  }
  return "";
}

void GiftStation::note(const std::string& line) {
  log_ += line + "\n";
  SDL_Log("gift station: %s", line.c_str());
}

void GiftStation::fail(const std::string& why) {
  note("failed: " + why);
  status_ = why;
  stage_ = Stage::Failed;
}

void GiftStation::reset_link() {
  connected_ = false;
  game_spoke_ = false;
  last_sent_ = 0;
  ni_in_ = {};
  ni_out_ = {};
  uni_ = false;
  game_joined_ = false;
  my_cmds_.clear();
  echo_pending_ = false;
  block_in_ = {};
  link_step_ = 0;
  wait_ = 0;
  mg_state_ = 0;
  mg_in_.clear();
  mg_in_header_ = false;
  mg_received_.clear();
  mg_received_ident_.clear();
  to_game_.clear();
}

void GiftStation::update() {
  if (!started_) return;
  ++frame_;
  if (!connected_) {
    if (frame_ % kBroadcastEvery == 1) broadcast();
  } else {
    if (frame_ - last_sent_ > 240) {
      note("the game stopped answering");
      reset_link();
      if (stage_ != Stage::Done) stage_ = Stage::Waiting;
    } else {
      if (link_step_ == 0 && game_joined_) {
        // As the host's link layer does once a game joins: everyone's player number, then
        // "send me your player data" (a 200-byte block each way).
        if (wait_ > 0) {
          --wait_;
        } else {
          queue_cmd(kCmdPlayerIds, {2, 0x0001});  // two players; slot 0 (the game) is player 1
          queue_cmd(kCmdBlockReq, {0});
          queue_block(link_player_block());
          link_step_ = 1;
        }
      }
      if (link_step_ >= 2) mg_step();
      if (game_spoke_ || frame_ - last_sent_ >= 8) host_frame();
    }
  }
  deliver();
  if (core_->poll) core_->poll();
}

// ---------------------------------------------------------------------------------------------
// gpSP's adapter messages

// What a distribution machine advertises: Nintendo's wireless library sends the serial number,
// the 13-byte "game name" (Game Freak's RfuGameData) and the 8-byte user name, with a checksum.
void GiftStation::broadcast() {
  uint8_t gname[13] = {};
  const uint16_t compat = 2 /* English */ | 1 << 5 /* has a Wonder Card */ | 3 << 10 /* Emerald */;
  gname[0] = static_cast<uint8_t>(compat);
  gname[1] = static_cast<uint8_t>(compat >> 8);
  gname[2] = 0x39;  // trainer id: any, but not zero
  gname[3] = 0x30;
  gname[10] = 21;  // activity: Wonder Card
  // "EVENT" in the games' character set, then the terminator.
  const uint8_t uname[8] = {0xBF, 0xD0, 0xBF, 0xC8, 0xCE, 0xFF, 0x00, 0x00};
  uint8_t bytes[24] = {};
  bytes[0] = 0x7D;  // serial number 0x7F7D: a Wonder Card / News distributor
  bytes[1] = 0x7F;
  std::memcpy(&bytes[2], gname, 13);
  uint8_t sum = 0;
  for (int i = 0; i < 8; ++i) sum = static_cast<uint8_t>(sum + uname[i] + gname[i]);
  bytes[15] = static_cast<uint8_t>(~sum);
  std::memcpy(&bytes[16], uname, 8);

  std::vector<uint8_t> p;
  put32be(p, kRfuMagic);
  put32be(p, kBroadcast);
  put32be(p, kHostDevice);
  for (int w = 0; w < 6; ++w) {
    const uint8_t* b = &bytes[w * 4];
    put32be(p, static_cast<uint32_t>(b[0]) | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 |
                   static_cast<uint32_t>(b[3]) << 24);
  }
  to_game_.push_back(std::move(p));
}

void GiftStation::send_msg(uint32_t type, uint32_t header, const uint8_t* data, std::size_t len) {
  std::vector<uint8_t> p;
  put32be(p, kRfuMagic);
  put32be(p, type);
  put32be(p, header);
  if (data) {
    p.insert(p.end(), data, data + len);
  } else {
    put32be(p, 0);
  }
  to_game_.push_back(std::move(p));
}

void GiftStation::from_game(const uint8_t* data, std::size_t len) {
  if (len < 12 || get32be(data) != kRfuMagic) return;
  const uint32_t type = get32be(data + 4), header = get32be(data + 8);
  switch (type) {
    case kConnectReq:
      note("the game connects");
      reset_link();
      connected_ = true;
      last_sent_ = frame_;
      if (stage_ != Stage::Done) stage_ = Stage::Connecting;
      send_msg(kConnectAck, kGameDevice);  // slot 0
      break;
    case kDisconnect:
      note("the game disconnected");
      reset_link();
      if (stage_ != Stage::Done && stage_ != Stage::Failed) stage_ = Stage::Waiting;
      break;
    case kClientSend:
      if (connected_) {
        game_spoke_ = true;
        last_sent_ = frame_;
        game_frames(data + 12, std::min<std::size_t>(header >> 24, len - 12));
      }
      break;
    case kClientAck:
      break;
    default:
      break;
  }
}

void GiftStation::deliver() {
  while (core_ && !to_game_.empty()) {
    std::vector<uint8_t> p = std::move(to_game_.front());
    to_game_.pop_front();
    core_->receive(p.data(), p.size(), kStationId);
  }
}

void RETRO_CALLCONV GiftStation::send_cb(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  if (!g_station || !buf) return;
  if (client_id != kStationId && client_id != 0xFFFF) return;
  g_station->from_game(static_cast<const uint8_t*>(buf), len);
}

void RETRO_CALLCONV GiftStation::poll_cb() {
  if (g_station) g_station->deliver();
}

// ---------------------------------------------------------------------------------------------
// Nintendo's wireless library: the frames in each packet

// A game (child) packet: frames with a 2-byte header: size in bits 0-4, phase 5-6, sequence
// number 7-8, acknowledgement 9, kind 10-13.
void GiftStation::game_frames(const uint8_t* p, std::size_t n) {
  bool saw_ni_end = false, any = false;
  std::size_t i = 0;
  while (i + 2 <= n) {
    const uint16_t h = get16(p + i);
    i += 2;
    const std::size_t size = h & 0x1F;
    const int phase = (h >> 5) & 3, seq = (h >> 7) & 3, ack = (h >> 9) & 1, kind = (h >> 10) & 0xF;
    if (i + size > n) break;
    any = true;
    if (kind == kUni) {
      if (size >= 14) {
        Cmd c{};
        for (std::size_t w = 0; w < 7; ++w) c[w] = get16(p + i + 2 * w);
        game_cmd(c);
      }
    } else if (ack) {
      ni_ack(kind, phase, seq);
    } else {
      if (kind == kNiEnd) saw_ni_end = true;
      ni_frame(kind, ack, phase, seq, p + i, size);
    }
    i += size;
  }
  // The receiving side finishes once the sender stops sending its end frame.
  if (any && ni_in_.state == 3 && !saw_ni_end) {
    ni_in_.state = 4;
    if (ni_in_.control[0] == 1) {
      note("the game sent its name");
      // Let it join: "OK" (the status a Union Room host sends), one byte.
      ni_send({kJoinGroupOk}, 8);
    }
  }
}

// Receiving a reliable (NI) transfer from the game: a 7-byte control part (type, piece size,
// total size), then the data in four interleaved windows, then an end frame. Every frame is
// answered with the state, window and sequence number seen (see host_frame).
void GiftStation::ni_frame(int kind, int /*ack*/, int phase, int seq, const uint8_t* data, std::size_t size) {
  NiRecv& r = ni_in_;
  bool check = false;
  if (kind == kNiEnd) {
    if (r.state == 2) {
      r.state = 3;
      r.phase = 0;
      r.n[0] = 0;
    }
  } else if (kind == kNi) {
    if (r.state == 1 && r.remain == 0) {
      r.payload = get16(&r.control[1]);
      const std::size_t total = static_cast<std::size_t>(r.control[3] | r.control[4] << 8 | r.control[5] << 16 | r.control[6] << 24);
      r.data.assign(std::min<std::size_t>(total, 4096), 0);
      for (std::size_t w = 0; w < 4; ++w) {
        r.n[w] = 0;
        r.at[w] = r.payload * w;
      }
      r.remain = r.data.size();
      r.state = 2;
    }
    check = r.state == 2;
  } else if (kind == kNiStart) {
    if (r.state != 1) {
      r = {};
      r.control.assign(7, 0);
      r.remain = 7;
      r.state = 1;
    }
    check = true;
  }
  if (check && seq == ((r.n[static_cast<std::size_t>(phase)] + 1) & 3)) {
    std::vector<uint8_t>& dst = r.state == 1 ? r.control : r.data;
    std::size_t& at = r.at[static_cast<std::size_t>(phase)];
    for (std::size_t k = 0; k < size && at + k < dst.size(); ++k) dst[at + k] = data[k];
    at += size;
    if (r.state == 2) at += 3 * r.payload;
    r.remain -= std::min(r.remain, size);
    r.n[static_cast<std::size_t>(phase)] = static_cast<uint8_t>(seq);
  }
  if (r.state >= 1 && r.state <= 3) r.phase = phase;
}

// Sending a reliable transfer to the game, in pieces of sub_frame bytes (header included).
void GiftStation::ni_send(const std::vector<uint8_t>& data, int sub_frame) {
  NiSend& s = ni_out_;
  s = {};
  s.payload = static_cast<std::size_t>(sub_frame - 3);
  s.data = data;
  s.control = {0, static_cast<uint8_t>(s.payload), static_cast<uint8_t>(s.payload >> 8),
               static_cast<uint8_t>(data.size()), static_cast<uint8_t>(data.size() >> 8), 0, 0};
  s.n = {1, 1, 1, 1};
  s.state = 1;
}

// The game acknowledged a frame of the station's transfer.
void GiftStation::ni_ack(int kind, int phase, int seq) {
  NiSend& s = ni_out_;
  const std::size_t p = static_cast<std::size_t>(phase);
  if (!((s.state == 1 && kind == kNiStart) || (s.state == 2 && kind == kNi) || (s.state == 3 && kind == kNiEnd))) return;
  if (s.n[p] != seq) return;
  s.n[p] = static_cast<uint8_t>((s.n[p] + 1) & 3);
  if (s.state == 1) {
    s.control_at += s.payload;
    if (s.control_at >= s.control.size()) {
      s.state = 2;
      for (std::size_t w = 0; w < 4; ++w) {
        s.n[w] = 1;
        s.at[w] = s.payload * w;
      }
      s.remain = s.data.size();
      s.phase = 0;
    }
  } else if (s.state == 2) {
    s.at[p] += s.payload * 4;
    if (s.remain <= s.payload) {
      s.remain = 0;
      s.state = 3;
      s.n[0] = 0;
      s.phase = 0;
    } else {
      s.remain -= s.payload;
    }
  } else {
    s.state = 4;  // one more, empty frame, and done
  }
}

// One station (parent) packet: frames with a 3-byte header: size in bits 0-6, phase 9-10,
// sequence number 11-12, acknowledgement 13, kind 14-17, the slots it is for 18-21.
void GiftStation::host_frame() {
  std::vector<uint8_t> frames;
  auto header = [&](int kind, int ack, int phase, int seq, std::size_t size) {
    const uint32_t h = static_cast<uint32_t>(size & 0x7F) | static_cast<uint32_t>(phase) << 9 |
                       static_cast<uint32_t>(seq) << 11 | static_cast<uint32_t>(ack) << 13 |
                       static_cast<uint32_t>(kind) << 14 | 1u << 18;
    frames.push_back(static_cast<uint8_t>(h));
    frames.push_back(static_cast<uint8_t>(h >> 8));
    frames.push_back(static_cast<uint8_t>(h >> 16));
  };

  NiSend& s = ni_out_;
  if (s.state == 1) {
    const std::size_t size = std::min(s.payload, s.control.size() - s.control_at);
    header(kNiStart, 0, 0, s.n[0], size);
    frames.insert(frames.end(), s.control.begin() + static_cast<std::ptrdiff_t>(s.control_at),
                  s.control.begin() + static_cast<std::ptrdiff_t>(s.control_at + size));
  } else if (s.state == 2) {
    for (int k = 0; k < 4 && s.at[static_cast<std::size_t>(s.phase)] >= s.data.size(); ++k) s.phase = (s.phase + 1) & 3;
    const std::size_t p = static_cast<std::size_t>(s.phase);
    const std::size_t size = std::min(s.payload, s.data.size() - std::min(s.data.size(), s.at[p]));
    header(kNi, 0, s.phase, s.n[p], size);
    frames.insert(frames.end(), s.data.begin() + static_cast<std::ptrdiff_t>(s.at[p]),
                  s.data.begin() + static_cast<std::ptrdiff_t>(s.at[p] + size));
    s.phase = (s.phase + 1) & 3;
  } else if (s.state == 3) {
    header(kNiEnd, 0, 0, s.n[0], 0);
  } else if (s.state == 4) {
    header(kNull, 0, 0, 0, 0);
    s.state = 5;
    note("the game may join");
    uni_ = true;
  }

  NiRecv& r = ni_in_;
  if (r.state >= 1 && r.state <= 3) header(r.state, 1, r.phase, r.n[static_cast<std::size_t>(r.phase)], 0);

  if (uni_) {
    // Every player's commands this frame: the station's own (player 0), then the game's last one
    // sent back (player 1), so it knows it arrived.
    std::array<uint8_t, 70> cmds{};
    if (!my_cmds_.empty()) {
      const Cmd c = my_cmds_.front();
      my_cmds_.pop_front();
      for (std::size_t w = 0; w < 7; ++w) cmds[2 * w] = static_cast<uint8_t>(c[w]), cmds[2 * w + 1] = static_cast<uint8_t>(c[w] >> 8);
    }
    if (echo_pending_) {
      for (std::size_t w = 0; w < 7; ++w)
        cmds[14 + 2 * w] = static_cast<uint8_t>(echo_[w]), cmds[14 + 2 * w + 1] = static_cast<uint8_t>(echo_[w] >> 8);
      echo_pending_ = false;
    }
    header(kUni, 0, 0, 0, cmds.size());
    frames.insert(frames.end(), cmds.begin(), cmds.end());
  }

  if (frames.empty()) frames.push_back(0xFF);  // nothing to say: the library's empty packet
  send_msg(kHostSend, static_cast<uint32_t>(frames.size()), frames.data(), frames.size());
  game_spoke_ = false;
  last_sent_ = frame_;
}

// ---------------------------------------------------------------------------------------------
// Game Freak's link layer

void GiftStation::game_cmd(Cmd c) {
  if (!game_joined_) {
    // The game's first per-frame message (even an empty one): it has joined, and waits for the
    // host to number the players.
    game_joined_ = true;
    if (stage_ != Stage::Done) stage_ = Stage::Linked;
    note("linked");
    wait_ = 4;
  }
  c[0] = static_cast<uint16_t>(c[0] & ~0x00E0);  // the game's own sequence number
  if ((c[0] & 0xFF00) == 0) return;
  echo_ = c;
  echo_pending_ = true;
  switch (c[0] & 0xFF00) {
    case kCmdBlockInit:
      if (!block_in_.receiving) {
        block_in_.receiving = true;
        block_in_.count = std::clamp<int>(c[1], 1, 32);
        block_in_.got = 0;
        block_in_.data.assign(static_cast<std::size_t>(block_in_.count) * 12, 0);
      }
      break;
    case kCmdBlock:
      if (block_in_.receiving) {
        const int idx = c[0] & 0xFF;
        if (idx < block_in_.count) {
          for (std::size_t w = 0; w < 6; ++w) {
            block_in_.data[static_cast<std::size_t>(idx) * 12 + 2 * w] = static_cast<uint8_t>(c[w + 1]);
            block_in_.data[static_cast<std::size_t>(idx) * 12 + 2 * w + 1] = static_cast<uint8_t>(c[w + 1] >> 8);
          }
          block_in_.got |= 1u << idx;
          const uint32_t all = block_in_.count >= 32 ? 0xFFFFFFFFu : (1u << block_in_.count) - 1;
          if (block_in_.got == all) {
            block_in_.receiving = false;
            block_done(block_in_.data);
          }
        }
      }
      break;
    case kCmdReadyExitStandby:
      queue_cmd(kCmdReadyExitStandby, {c[1]});
      break;
    case kCmdReadyCloseLink:
      queue_cmd(kCmdReadyCloseLink, {c[1]});
      note("the game closes the link");
      break;
    default:
      break;
  }
}

void GiftStation::queue_cmd(uint16_t c0, std::initializer_list<uint16_t> words) {
  Cmd c{};
  c[0] = c0;
  std::size_t i = 1;
  for (uint16_t w : words) {
    if (i < c.size()) c[i++] = w;
  }
  my_cmds_.push_back(c);
}

// A block as the host sends one: "a block of N pieces" four times, then the pieces, 12 bytes each.
void GiftStation::queue_block(const std::vector<uint8_t>& data) {
  const std::size_t count = std::max<std::size_t>(1, (data.size() + 11) / 12);
  std::vector<uint8_t> padded(data);
  padded.resize(count * 12 + 2, 0);
  for (int k = 0; k < 4; ++k) queue_cmd(kCmdBlockInit, {static_cast<uint16_t>(count), 0x80});
  for (std::size_t i = 0; i < count; ++i) {
    Cmd c{};
    c[0] = static_cast<uint16_t>(kCmdBlock | i);
    for (std::size_t w = 0; w < 6; ++w) c[w + 1] = get16(&padded[i * 12 + 2 * w]);
    my_cmds_.push_back(c);
  }
  // A pause, so the game has taken the block before the next one starts.
  for (int k = 0; k < 6; ++k) my_cmds_.push_back(Cmd{});
}

// The station's player, as the game sees it in the link: Game Freak's LinkPlayerBlock.
std::vector<uint8_t> GiftStation::link_player_block() const {
  std::vector<uint8_t> b(200, 0);
  const char magic[] = "GameFreak inc.";
  std::memcpy(&b[0], magic, sizeof magic);
  std::memcpy(&b[44], magic, sizeof magic);
  uint8_t* lp = &b[16];
  lp[0] = 3, lp[1] = 0x40;     // version: Emerald
  lp[2] = 0x00, lp[3] = 0x80;
  lp[4] = 0x39, lp[5] = 0x30;  // trainer id (as broadcast)
  const uint8_t name[8] = {0xBF, 0xD0, 0xBF, 0xC8, 0xCE, 0xFF, 0x00, 0x00};  // EVENT
  std::memcpy(lp + 8, name, 8);
  lp[26] = 2;  // English
  return b;
}

void GiftStation::block_done(const std::vector<uint8_t>& data) {
  if (link_step_ == 1) {
    if (std::memcmp(data.data(), "GameFreak inc.", 14) == 0) {
      link_step_ = 2;
      note("player exchange done");
      wait_ = 30;
    } else {
      fail("the game's player data was not recognised");
    }
    return;
  }
  // A Mystery Gift transfer: a 6-byte header (id, CRC, size), then the data in 252-byte blocks.
  if (!mg_in_header_) {
    mg_in_ident_ = get16(&data[0]);
    mg_in_crc_ = get16(&data[2]);
    mg_in_size_ = get16(&data[4]);
    mg_in_.clear();
    mg_in_header_ = true;
    return;
  }
  const std::size_t want = std::min<std::size_t>(252, mg_in_size_ - mg_in_.size());
  mg_in_.insert(mg_in_.end(), data.begin(), data.begin() + static_cast<std::ptrdiff_t>(std::min(want, data.size())));
  if (mg_in_.size() >= mg_in_size_) {
    mg_in_header_ = false;
    if (game_crc16(mg_in_.data(), mg_in_.size()) != mg_in_crc_) note("transfer " + std::to_string(mg_in_ident_) + ": bad CRC");
    mg_received_.push_back(mg_in_);
    mg_received_ident_.push_back(mg_in_ident_);
  }
}

// ---------------------------------------------------------------------------------------------
// Mystery Gift: the server's side, as a distribution machine ran it

std::vector<uint8_t> GiftStation::client_script(std::initializer_list<std::pair<uint32_t, uint32_t>> cmds) const {
  std::vector<uint8_t> s;
  for (const auto& [instr, param] : cmds) {
    put16(s, static_cast<uint16_t>(instr)), put16(s, static_cast<uint16_t>(instr >> 16));
    put16(s, static_cast<uint16_t>(param)), put16(s, static_cast<uint16_t>(param >> 16));
  }
  return s;
}

void GiftStation::mg_send(uint16_t ident, std::vector<uint8_t> data) {
  std::vector<uint8_t> header;
  put16(header, ident);
  put16(header, game_crc16(data.data(), data.size()));
  put16(header, static_cast<uint16_t>(data.size()));
  queue_block(header);
  for (std::size_t at = 0; at < data.size(); at += 252)
    queue_block(std::vector<uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(at),
                                     data.begin() + static_cast<std::ptrdiff_t>(std::min(data.size(), at + 252))));
}

// The game holds this card already: a stamp for a Stamp Card, the trainer for a Visiting Trainer
// card whose password the survey has, else nothing new.
void GiftStation::holder_visit(const std::vector<uint8_t>& in) {
  auto had_card = [&] {
    note("the game has this card already");
    mg_send(kMgClientScript, client_script({{kCliSendReadyEnd, 0}, {kCliReturn, kCliMsgHadCard}}));
    mg_state_ = 9;
  };
  if (!gift_.trainer.empty()) {
    bool match = gift_.password.size() == 4;
    for (std::size_t i = 0; match && i < 4; ++i) match = get16(&in[kGameDataWords + 2 * i]) == gift_.password[i];
    if (!match) return had_card();
    note("the survey has the password: sending the trainer");
    mg_send(kMgClientScript, client_script({{kCliRecv, kMgEReaderTrainer}, {kCliRecvEReaderTrainer, 0}, {kCliSendReadyEnd, 0},
                                            {kCliReturn, kCliMsgTrainerReceived}}));
    mg_send(kMgEReaderTrainer, gift_.trainer);
    mg_state_ = 4;
    return;
  }
  if (gift_.stamps.empty()) return had_card();
  const std::size_t max = std::min<std::size_t>(in[kGameDataMaxStamps], 7);
  std::size_t have = 0;
  auto on_card = [&](uint32_t stamp) {
    for (std::size_t i = 0; i < max; ++i) {
      if (get16(&in[kGameDataStampIds + 2 * i]) == (stamp >> 16) || get16(&in[kGameDataStampSpecies + 2 * i]) == (stamp & 0xFFFF))
        return true;
    }
    return false;
  };
  for (std::size_t i = 0; i < max; ++i) have += get16(&in[kGameDataStampIds + 2 * i]) && get16(&in[kGameDataStampSpecies + 2 * i]);
  const auto next = std::find_if(gift_.stamps.begin(), gift_.stamps.end(), [&](uint32_t st) { return !on_card(st); });
  if (have >= max || next == gift_.stamps.end()) {
    note("the stamp card is full");
    mg_send(kMgClientScript, client_script({{kCliSendReadyEnd, 0}, {kCliReturn, have >= max ? kCliMsgNoRoomStamps : kCliMsgHadStamp}}));
    mg_state_ = 9;
    return;
  }
  note("stamping the card");
  std::vector<uint8_t> stamp;
  put16(stamp, static_cast<uint16_t>(*next)), put16(stamp, static_cast<uint16_t>(*next >> 16));
  mg_send(kMgClientScript, client_script({{kCliRecv, kMgStamp}, {kCliSaveStamp, 0}, {kCliSendReadyEnd, 0},
                                          {kCliReturn, kCliMsgStampReceived}}));
  mg_send(kMgStamp, stamp);
  mg_state_ = 4;
}

void GiftStation::mg_step() {
  if (wait_ > 0) {
    --wait_;
    return;
  }
  if (!my_cmds_.empty()) return;  // still sending
  auto take = [&](uint16_t ident, std::vector<uint8_t>& out) {
    if (mg_received_.empty()) return false;
    if (mg_received_ident_.front() != ident) {
      fail("the game sent " + std::to_string(mg_received_ident_.front()) + ", not " + std::to_string(ident));
      return false;
    }
    out = std::move(mg_received_.front());
    mg_received_.pop_front();
    mg_received_ident_.erase(mg_received_ident_.begin());
    return true;
  };
  std::vector<uint8_t> in;
  switch (mg_state_) {
    case 0:  // ask for the game's data
      stage_ = Stage::Sending;
      note("asking for the game's data");
      mg_send(kMgClientScript, client_script({{kCliLoadGameData, 0}, {kCliSendLoaded, 0}, {kCliRecv, kMgClientScript}, {kCliCopyRecv, 0}}));
      mg_state_ = 1;
      break;
    case 1:  // the game's data: has it a Wonder Card already?
      if (!take(kMgGameData, in)) break;
      if (in.size() < 22 || get16(&in[0]) != 0x101) {
        note("the game's data is not what a distributor expects");
        mg_send(kMgClientScript, client_script({{kCliSendReadyEnd, 0}, {kCliReturn, kCliMsgCantAccept}}));
        mg_state_ = 9;
      } else if (get16(&in[kGameDataCardFlag]) == gift_.card_flag && in.size() > kGameDataMaxStamps) {
        holder_visit(in);
      } else if (get16(&in[kGameDataCardFlag]) != 0) {
        note("the game has another card: asking to replace it");
        mg_send(kMgClientScript, client_script({{kCliAskToss, 0}, {kCliLoadTossResponse, 0}, {kCliSendLoaded, 0},
                                                {kCliRecv, kMgClientScript}, {kCliCopyRecv, 0}}));
        mg_state_ = 2;
      } else {
        mg_state_ = 3;
      }
      break;
    case 2:  // replace the old card?
      if (!take(kMgResponse, in)) break;
      if (in.size() >= 4 && in[0] == 0) {
        mg_state_ = 3;
      } else {
        note("the player kept the old card");
        mg_send(kMgClientScript, client_script({{kCliRecv, kMgDynamicMsg}, {kCliCopyMsg, 0}, {kCliSendReadyEnd, 0},
                                                {kCliReturn, kCliMsgBufferFailure}}));
        mg_send(kMgDynamicMsg, encode_game_text("You kept your old\nWONDER CARD."));
        mg_state_ = 9;
      }
      break;
    case 3:  // the card, then the deliveryman's script
      note("sending the card");
      mg_send(kMgClientScript, client_script({{kCliRecv, kMgCard}, {kCliSaveCard, 0}, {kCliRecv, kMgRamScript},
                                              {kCliSaveRamScript, 0}, {kCliSendReadyEnd, 0}, {kCliReturn, kCliMsgCardReceived}}));
      mg_send(kMgCard, gift_.card);
      mg_send(kMgRamScript, gift_.script);
      mg_state_ = 4;
      break;
    case 4:
      if (!take(kMgReadyEnd, in)) break;
      note("the game has the card");
      stage_ = Stage::Done;
      mg_state_ = 10;
      break;
    case 9:
      if (!take(kMgReadyEnd, in)) break;
      note("nothing sent");
      mg_state_ = 10;
      break;
    default:
      break;
  }
}

}  // namespace p64
