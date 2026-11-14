// GBA link play: the link cable or the Wireless Adapter between two to four players' games, over
// the group's relay. Unlike N64 online play there is no lockstep: each player's game runs on its
// own computer at full speed, and only what goes over the link travels. The core (gpSP) speaks
// the link protocols itself and hands their messages over through the libretro netpacket
// interface; this class carries them between the players.
//
// Room ids come from the relay: the host is 0, the others 1 to 3, which is what the netpacket
// interface uses too. Every message starts with a byte saying what it is: a packet from the
// core, "hello" (the player's name and link mode, so both ends can say when they differ), or the
// app's version (since 0.2.9; apps before it ignore it and don't send one), since both ends need
// the same emulator core: the link cable's timing changed in 0.2.7.
//
// A connection that breaks is taken up again (since 0.2.13). The games stop where they are, each
// app connects to the room again, and what the core sent that didn't arrive is sent again, in
// order, so the games carry on as if nothing happened:
// - Every core packet to a player carries a number counting up from 1 (kSeq). The receiver
//   passes them to its core strictly in order and drops what it has already had.
// - After a (re)connection each side tells the other the last number it received (kResume); the
//   other sends everything after it again from the packets it keeps (the last few thousand).
// - A player who leaves on purpose says so first (kBye), so nobody waits for them.
// - The relay is asked for a sign of life every few seconds (net.hpp): a connection gone silent
//   is replaced before the games notice.
// A guest can always come back; a host needs the relay from 0.2.13 on, which gives it its room
// code again (an older relay hands out a new code, and the link ends as it used to).
#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"
#include "link_pacer.hpp"
#include "net.hpp"

namespace p64 {

class Link {
 public:
  enum class Phase { Off, Connecting, Open, Reconnecting };
  struct Peer {
    std::string name, mode, game, version;
    bool version_told = false;
    bool can_resume = false;  // its app numbers its packets and can take a broken link up again
    bool said_bye = false;    // it is leaving on purpose
    bool away = false;        // its connection broke; waiting for it to come back
    // It is in the room again after `away`: whether it is the same link coming back (its session
    // matches) or the app started a new one is known from its kCaps.
    bool rejoining = false;
    uint32_t session = 0;     // its link's session (kCaps; 0: not told, an app from 0.2.13)
  };

  ~Link();

  // `mode` is this game's link mode ("cable" or "wireless"), `game` its title: shown to the others.
  void host(const std::string& relay_url, const std::string& key, const std::string& name, const std::string& mode,
            const std::string& game);
  void join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
            const std::string& mode, const std::string& game);
  void leave();
  // This app's version, sent to the others so a mismatch can be told.
  void set_version(const std::string& version) { version_ = version; }

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
  // After each frame the game runs while linked: counts it, and every few frames tells the others.
  void frame_done();
  // The game has run too far beyond another player's (a stalled connection) and should wait for
  // it (link_pacer.hpp).
  bool waiting() const { return waiting_for() != Wait::None; }
  // Why: the other game is behind (a stall), or a connection broke and is being taken up again.
  enum class Wait { None, Pace, Reconnect };
  Wait waiting_for() const;
  void give_up_waiting();
  // The group's relay can give a host its room again after a dropped connection (0.2.13 on).
  // Known once the link is open.
  bool relay_resumes() const { return !token_.empty(); }

  // For the log (link_monitor.hpp): what went over the link since the last call, which resets it.
  struct Stats {
    uint64_t sent = 0, received = 0;            // the core's packets
    uint64_t sent_data = 0, received_data = 0;  // of those, the cable packets carrying game data
    uint64_t bytes_sent = 0, bytes_received = 0;
    int my_cable = -1, their_cable = -1;  // gpSP's cable state last sent and heard (-1: none yet)
    uint64_t frames = 0;                  // this game's frames since the link started
    bool paced = false;                   // the other's frames are known
    uint64_t their_frames = 0;
    int64_t lead = 0;  // frames this game is ahead of the other, past their starting gap
    int pings = 0;     // round trips to the other player's app, in milliseconds
    double ping_min = 0, ping_max = 0, ping_avg = 0;
    std::size_t backlog = 0;  // messages queued, not yet sent
    int relay_silent_ms = -1;  // how long the relay itself has been silent (-1: not known)
    uint64_t resent = 0, dropped = 0;  // packets sent again after a reconnection; duplicates dropped
    int reconnects = 0;                // connections taken up again since the link started
  };
  Stats take_stats();

 private:
  // kPing and kPong (since 0.2.12) measure the time to the other player and back; kBye, kResume,
  // kCaps and kSeq (since 0.2.13) take a broken link up again. Older apps ignore what they don't know.
  enum Kind : uint8_t { kCore = 1, kHello = 2, kVersion = 3, kTick = 4, kPing = 5, kPong = 6, kBye = 7, kResume = 8, kCaps = 9, kSeq = 10 };
  static constexpr uint8_t kBroadcast = 255;

  void open(const std::string& url);
  std::string url(bool again) const;
  void begin_reconnect(const std::string& why);
  void fail(const std::string& message);
  void send_resume(int to);
  void peer_back(int id);
  void drop_peer(int id);
  void deliver(int from, const char* data, std::size_t len);
  void on_text(const std::string& text);
  void on_binary(const std::string& data);
  void on_closed(const net::WebSocket::Event& e);
  void say_hello(int to);
  void start_core();
  void stop_core();

  // The core's callbacks have no user pointer: they go to the one link that is running.
  static void RETRO_CALLCONV send_cb(int flags, const void* buf, size_t len, uint16_t client_id);
  static void RETRO_CALLCONV poll_cb();

  net::WebSocket ws_;
  net::WebSocket farewell_;  // a goodbye sent after leaving while the connection was down
  Phase phase_ = Phase::Off;
  int me_ = -1;
  std::string code_, status_, name_, mode_, game_, version_;
  std::map<int, Peer> peers_;
  std::vector<std::string> messages_;
  const retro_netpacket_callback* core_ = nullptr;
  bool core_started_ = false;
  uint64_t frames_ = 0;              // frames run since the core's link started
  std::map<int, LinkPacer> pacers_;  // one for each other player
  // What makes a broken link resumable, for each other player.
  struct Line {
    uint32_t sent = 0, received = 0;  // the numbers of the last packet sent to and received from it
    std::deque<std::string> kept;     // the last packets sent, to send again (the newest is number `sent`)
    bool synced = true;               // since the last (re)connection it has said what it received
    uint64_t since_ns = 0;            // when it went away, or the wait for its word began
    uint64_t asked_ns = 0;            // when it was last asked what it received
  };
  std::map<int, Line> lines_;
  std::string relay_url_, key_, token_;
  bool hosting_ = false;
  uint64_t lost_ns_ = 0, retry_ns_ = 0;
  uint32_t session_ = 0;  // this link's session, new for each host() or join(), told in kCaps
  int reconnects_ = 0;
  uint64_t resent_ = 0, dropped_ = 0;
  Stats stats_;
  double ping_sum_ = 0;
  uint64_t last_ping_ns_ = 0;
  void count_core(const void* data, std::size_t len, bool sent);
};

}  // namespace p64
