#include "link_monitor.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace p64 {
namespace {

// pret symbols (pokeemerald, pokeruby, pokefirered/pokeleafgreen .sym): gLink, gLinkStatus,
// gReceivedRemoteLinkPlayers, gLinkType, gLinkPlayers, sLinkErrorBuffer (Ruby and Sapphire
// have none).
constexpr LinkMonitor::Layout kEmerald = {0x03003170, 0x030030E0, 0x03003124, 0x020229C6, 0x020229E8, 0x02022B00};
constexpr LinkMonitor::Layout kRubySapphire = {0x03003070, 0x03002A60, 0x03002FA4, 0x03002B74, 0x03002970, 0};
constexpr LinkMonitor::Layout kFireRedLeafGreen = {0x03003FB0, 0x03003F20, 0x03003F64, 0x0202271A, 0x0202273C, 0x02022854};

const LinkMonitor::Layout* layout_for(const std::string& code) {
  if (code == "BPEE") return &kEmerald;
  if (code == "AXVE" || code == "AXPE") return &kRubySapphire;
  if (code == "BPRE" || code == "BPGE") return &kFireRedLeafGreen;
  return nullptr;
}

// struct Link (pret include/link.h, the same in all five): its fields, then the send queue (from
// 0x18; its count at 0x339) and the receive queue (from 0x33C; its count at 0xFBD), each 50
// commands long.
constexpr uint32_t kLinkSize = 0xFC0, kSendCount = 0x339, kRecvCount = 0xFBD;
constexpr int kQueueCapacity = 50;

uint32_t read32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

const char* link_state_name(int s) {
  static const char* const kNames[] = {"idle", "starting", "handshake", "timer", "connected"};
  return s >= 0 && s <= 4 ? kNames[s] : "?";
}

std::string errors_text(uint32_t status, uint8_t hardware, uint8_t checksum, uint8_t queue_full, uint8_t lag) {
  std::string out;
  const auto add = [&out](const char* what) { out += out.empty() ? what : std::string(", ") + what; };
  if (hardware || (status & 0x1000)) add("hardware");
  if (checksum || (status & 0x2000)) add("checksum");
  if (queue_full || (status & 0x4000)) add(queue_full == 2 ? "receive queue full" : queue_full == 1 ? "send queue full" : "queue full");
  if (status & 0x10000) add("lag (master)");
  if (status & 0x20000) add("invalid id");
  if (status & 0x40000) add("lag (slave)");
  if (lag && !(status & 0x50000)) add(lag == 1 ? "lag (master)" : "lag (slave)");
  return out.empty() ? "none" : out;
}

// A linked player's name, in the games' own characters (enough for names).
std::string player_name(const uint8_t* p, int size) {
  std::string out;
  for (int i = 0; i < size && p[i] != 0xFF; ++i) {
    const uint8_t c = p[i];
    if (c >= 0xBB && c <= 0xD4) out += static_cast<char>('A' + (c - 0xBB));
    else if (c >= 0xD5 && c <= 0xEE) out += static_cast<char>('a' + (c - 0xD5));
    else if (c >= 0xA1 && c <= 0xAA) out += static_cast<char>('0' + (c - 0xA1));
    else if (c == 0x00) out += ' ';
    else out += '?';
  }
  return out;
}

const char* version_name(int v) {
  switch (v) {
    case 1: return "Sapphire";
    case 2: return "Ruby";
    case 3: return "Emerald";
    case 4: return "FireRed";
    case 5: return "LeafGreen";
    case 15: return "Colosseum/XD";
    default: return "?";
  }
}

const char* language_name(int l) {
  switch (l) {
    case 1: return "Japanese";
    case 2: return "English";
    case 3: return "French";
    case 4: return "Italian";
    case 5: return "German";
    case 7: return "Spanish";
    default: return "?";
  }
}

const char* cable_name(int s) {
  static const char* const kNames[] = {"starting", "handshake", "connected"};
  return s >= 0 && s <= 2 ? kNames[s] : "none yet";
}

}  // namespace

LinkMonitor::Game LinkMonitor::read_game(const GbaMemory& memory) const {
  Game g;
  if (!layout_) return g;
  const uint8_t* link = memory(layout_->link, kLinkSize);
  const uint8_t* status = memory(layout_->status, 4);
  const uint8_t* received = memory(layout_->received, 1);
  const uint8_t* type = memory(layout_->type, 2);
  if (!link || !status || !received || !type) return g;
  g.read = true;
  g.master = link[0];
  g.state = link[1];
  g.id = link[2];
  g.count = link[3];
  g.hardware = link[0x10];
  g.checksum = link[0x11];
  g.queue_full = link[0x12];
  g.lag = link[0x13];
  g.send = link[kSendCount];
  g.recv = link[kRecvCount];
  g.status = read32(status);
  g.received = *received;
  g.type = static_cast<uint16_t>(type[0] | type[1] << 8);
  if (layout_->error_buffer) {
    if (const uint8_t* e = memory(layout_->error_buffer, 8)) std::memcpy(g.error_buffer, e, 8);
  }
  return g;
}

void LinkMonitor::log_changes(const Game& g, const GbaMemory& memory) {
  if (!g.read) return;
  const Game& was = last_;
  // gLinkStatus: player count, master, connection, errors (the bits that flicker every transfer,
  // "received nothing" and the serial counter, are left out).
  // The game's state, its role and id are logged as they change; its player count and status
  // word flicker from frame to frame during a handshake, so they're logged once they hold for a
  // quarter of a second (or with the state).
  constexpr uint32_t kStatusBits = 0x1C | 0x20 | 0x40 | 0x7F000;
  const bool key_changed = !was.read || g.state != was.state || g.master != was.master || g.id != was.id;
  const uint32_t shown = (g.status & kStatusBits) | static_cast<uint32_t>(g.count) << 24;
  if (shown != pending_) {
    pending_ = shown;
    pending_samples_ = 0;
  } else if (pending_samples_ < 1000) {
    ++pending_samples_;
  }
  const bool settled = shown != logged_ && pending_samples_ == 15;
  if (key_changed || settled) {
    logged_ = shown;
    SDL_Log("game: link %s, %s, id %d, %d player%s; status 0x%05X (%s%s, %d players)", link_state_name(g.state),
            g.master ? "master" : "slave", g.id, g.count, g.count == 1 ? "" : "s", static_cast<unsigned>(g.status),
            (g.status & 0x40) ? "connection established" : "not connected", (g.status & 0x20) ? ", master" : "",
            static_cast<int>((g.status >> 2) & 7));
  }
  const bool errors = g.hardware || g.checksum || g.queue_full || g.lag || (g.status & 0x7F000);
  const bool were = was.read && (was.hardware || was.checksum || was.queue_full || was.lag || (was.status & 0x7F000));
  if (errors != were || (errors && (g.hardware != was.hardware || g.checksum != was.checksum ||
                                    g.queue_full != was.queue_full || g.lag != was.lag ||
                                    (g.status & 0x7F000) != (was.status & 0x7F000)))) {
    if (errors) {
      const std::string what = errors_text(g.status, g.hardware, g.checksum, g.queue_full, g.lag);
      SDL_Log("game: LINK ERROR: %s (send queue %d, receive queue %d of %d)", what.c_str(), g.send, g.recv, kQueueCapacity);
      if (!were) {
        notes_.push_back("Your game reports a link error (" + what + "). If it stops, both of you restart your games and "
                         "link again; the log (prometheus.log) has the details");
      }
    } else if (were) {
      SDL_Log("game: link errors cleared");
    }
  }
  static const uint8_t kZero[8] = {};
  if (std::memcmp(g.error_buffer, was.error_buffer, 8) != 0 && std::memcmp(g.error_buffer, kZero, 8) != 0) {
    // The game's own record of the error it shows ("Communication error"): its status word and
    // queue counts at that moment (pret link.c, sLinkErrorBuffer).
    const uint8_t* e = g.error_buffer;
    SDL_Log("game: the game recorded a link error: status 0x%08X, %s; queues %d and %d; %02X %02X",
            static_cast<unsigned>(read32(e)), errors_text(read32(e), 0, 0, 0, 0).c_str(), e[4], e[5], e[6], e[7]);
  }
  if (was.read && g.type != was.type) SDL_Log("game: link activity 0x%04X (was 0x%04X)", g.type, was.type);
  if (g.received != was.received || !was.read) {
    if (g.received) {
      // Each linked player as the game sees them (struct LinkPlayer, 0x1C bytes): game, language,
      // name, trainer ID.
      std::string who;
      const int n = g.count >= 1 && g.count <= 4 ? g.count : 2;
      for (int i = 0; i < n; ++i) {
        const uint8_t* p = memory(layout_->players + 0x1C * static_cast<uint32_t>(i), 0x1C);
        if (!p) break;
        const int version = p[0];
        const int language = p[0x1A] | p[0x1B] << 8;
        char line[160];
        std::snprintf(line, sizeof line, "%s%d: %s (%s), %s, ID %05u", who.empty() ? "" : "; ", i, version_name(version),
                      language_name(language), player_name(p + 8, 8).c_str(), static_cast<unsigned>(p[4] | p[5] << 8));
        who += line;
      }
      SDL_Log("game: linked players received: %s", who.c_str());
    } else if (was.read) {
      SDL_Log("game: linked players cleared");
    }
  }
  last_ = g;
}

void LinkMonitor::heartbeat(uint64_t now_ns, Link& link) {
  const Link::Stats s = link.take_stats();
  const double secs = static_cast<double>(now_ns - window_ns_) / 1e9;
  const uint64_t frames = s.frames >= frames_at_window_ ? s.frames - frames_at_window_ : s.frames;
  char pace[96] = "no word from the other yet";
  if (s.paced)
    std::snprintf(pace, sizeof pace, "%+lld frames ahead (waits past %lld), their frame %llu", static_cast<long long>(s.lead),
                  static_cast<long long>(LinkPacer::kWindow), static_cast<unsigned long long>(s.their_frames));
  char ping[96] = "no reply";
  if (s.pings)
    std::snprintf(ping, sizeof ping, "%.0f ms (%.0f-%.0f, %d)", s.ping_avg, s.ping_min, s.ping_max, s.pings);
  char relay[48] = "not yet";
  if (s.relay_silent_ms >= 0) std::snprintf(relay, sizeof relay, "%d ms ago", s.relay_silent_ms);
  char game[200] = "not read for this game";
  if (last_.read)
    std::snprintf(game, sizeof game, "%s, %s, %d players, send queue max %d, receive queue max %d of %d, errors %s",
                  link_state_name(last_.state), last_.master ? "master" : "slave", last_.count, send_max_, recv_max_,
                  kQueueCapacity, errors_text(last_.status, last_.hardware, last_.checksum, last_.queue_full, last_.lag).c_str());
  SDL_Log("link status (%.1f s): %.1f frames/s, held %.1f s, %s | sent %llu (%llu with game data), received %llu (%llu) | "
          "%llu/%llu bytes | queued %zu | round trip %s | relay heard %s | reconnected %d, sent again %llu, duplicates %llu | "
          "cable: this game %s, other %s | game: %s%s",
          secs, secs > 0 ? static_cast<double>(frames) / secs : 0.0, static_cast<double>(held_ns_) / 1e9, pace,
          static_cast<unsigned long long>(s.sent), static_cast<unsigned long long>(s.sent_data),
          static_cast<unsigned long long>(s.received), static_cast<unsigned long long>(s.received_data),
          static_cast<unsigned long long>(s.bytes_sent), static_cast<unsigned long long>(s.bytes_received), s.backlog, ping,
          relay, s.reconnects, static_cast<unsigned long long>(s.resent), static_cast<unsigned long long>(s.dropped),
          cable_name(s.my_cable), cable_name(s.their_cable), game, menu_ ? " | menu open" : "");
  window_ns_ = now_ns;
  frames_at_window_ = s.frames;
  held_ns_ = 0;
  send_max_ = last_.send;
  recv_max_ = last_.recv;
}

void LinkMonitor::update(uint64_t now_ns, Link& link, const std::string& game_code, const GbaMemory& memory, bool held,
                         bool menu) {
  if (!started_ns_ || game_code != code_) {
    started_ns_ = window_ns_ = last_ns_ = now_ns;
    code_ = game_code;
    layout_ = layout_for(game_code);
    last_ = Game{};
    held_ns_ = 0;
    frames_at_window_ = 0;
    send_max_ = recv_max_ = 0;
    pending_ = logged_ = 0;
    pending_samples_ = 0;
    menu_ = menu;
    SDL_Log("link log: game %s; the game's own link state %s", game_code.c_str(),
            layout_ ? "is read from its memory" : "isn't known for this game");
  }
  if (held) held_ns_ += std::min<uint64_t>(now_ns - last_ns_, 1'000'000'000ull);
  last_ns_ = now_ns;
  if (menu != menu_) {
    SDL_Log("link: the menu was %s (the game keeps running while linked)", menu ? "opened" : "closed");
    menu_ = menu;
  }
  const Game g = read_game(memory);
  log_changes(g, memory);
  if (g.read) {
    send_max_ = std::max<int>(send_max_, g.send);
    recv_max_ = std::max<int>(recv_max_, g.recv);
  }
  if (now_ns - window_ns_ >= 5'000'000'000ull) heartbeat(now_ns, link);
}

void LinkMonitor::stop(uint64_t now_ns, Link& link) {
  if (!started_ns_) return;
  heartbeat(now_ns, link);
  SDL_Log("link log: the link lasted %.1f s", static_cast<double>(now_ns - started_ns_) / 1e9);
  started_ns_ = 0;
  code_.clear();
}

}  // namespace p64
