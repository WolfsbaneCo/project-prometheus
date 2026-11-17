// Online play, lockstep: every computer runs the same game from the same start, and only
// controller inputs travel (through the relay). Each computer samples its own players a few
// frames ahead (the input delay) and sends that input to the others; a frame runs only once the
// input of every player for it has arrived, so every copy of the game stays identical. Every few
// seconds the computers compare a fingerprint of the console's memory to catch any drift.
//
// The host picks the game, the input delay and which computer drives which N64 port, sends its
// own save to the guests, and alone can pause or reset. Brief section 11.
//
// Keeping a game going when a connection fails (since 0.4.0), in layers:
// - Once the game starts, every message to a player carries a number (kSeq) and is kept; a
//   connection that breaks is taken up again with the relay (the same room, the same player id),
//   and each side tells the other the last number it received (kResume) so the rest is sent again.
//   The game holds still meanwhile (lockstep waits for the inputs) and carries on where it was.
// - A player whose connection to the relay breaks is waited for (45 seconds) instead of ending
//   the game; one leaving on purpose says so (kBye, or the host's kEnd).
// - A direct path between the computers (direct.hpp) carries the same numbered messages beside the
//   relay; the first copy of each is used. While the relay reconnects, the game doesn't even stop.
// - A connection gone silent is noticed in 6 seconds (the relay answers a ping every 2).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "direct.hpp"
#include "input.hpp"
#include "net.hpp"

namespace p64 {

class Netplay {
 public:
  static constexpr int kProtocol = 4;  // this app's own messages (the relay has its own version); 4: numbered, resumable
  static constexpr int kPorts = 4;
  // Up to a third of a second: enough for friends on different continents.
  static constexpr int kMinDelay = 2, kMaxDelay = 20;

  enum class Phase { Off, Connecting, Lobby, Starting, Running };

  struct GameInfo {
    std::string id, title, internal_name, region;
    // SHA-256 of the game as the console sees it (any file format): everyone needs the same release.
    std::array<uint8_t, 32> sha{};
  };

  struct Player {
    int id = -1;  // relay id: 0 = host
    std::string name;
    int ping_ms = -1;
    int local_players = 1;
    uint8_t rumble_mask = 0;  // which of its local players have a rumbling controller
    bool checked = false;     // guest has told the host whether it has the game
    std::string problem;      // why it can't play, empty when it can
    bool ready = false;       // loaded the game, waiting for the start
  };

  struct Port {
    int player = -1;  // relay id driving this port, -1 = nobody
    int local = 0;    // which of that computer's players
  };

  // Things the app acts on; collect them every frame with take_events().
  struct Event {
    enum Kind {
      Message,   // `text` for the player
      CheckGame, // guest: is `game` in the library? Answer with report_game()
      LoadGame,  // guest: start `game` with `save` as the in-game save, then call report_ready()
      Begin,     // everyone: run from frame 0 now
      Ended,     // the session (or lobby) is over; `text` says why
    } kind = Message;
    std::string text = {};
    GameInfo game = {};
    std::vector<uint8_t> save = {};
    bool keep_playing = false;  // Ended, host only: carry on offline
  };

  // Lobby.
  // `key` is the group's key (from its key file); joining also needs the host's room code.
  void host(const std::string& relay_url, const std::string& key, const std::string& name, const GameInfo& game,
            int local_players, uint8_t rumble_mask, const std::string& versions);
  void join(const std::string& relay_url, const std::string& key, const std::string& code, const std::string& name,
            int local_players, uint8_t rumble_mask, const std::string& versions);
  void leave();                 // disconnect from the room or session
  void update();                // pump the network, every frame
  std::vector<Event> take_events();
  void report_game(bool ok, const std::string& problem);  // guest
  void report_ready();                                     // guest
  bool can_start(std::string& why) const;                  // host
  void start(const std::vector<uint8_t>& save);             // host, with the game loaded and its save in place
  void set_delay(int frames);                              // host
  int suggested_delay() const;

  // State for the screens.
  Phase phase() const { return phase_; }
  bool active() const { return phase_ != Phase::Off; }
  bool running() const { return phase_ == Phase::Running; }
  bool is_host() const { return me_ == 0; }
  int my_id() const { return me_; }
  const std::string& code() const { return code_; }
  const std::string& status() const { return status_; }
  const std::vector<Player>& players() const { return players_; }
  const std::array<Port, kPorts>& ports() const { return ports_; }
  const GameInfo& game() const { return game_; }
  int delay() const { return delay_; }  // the delay the game started with
  int my_delay() const { return my_delay_; }  // this computer's delay now (it adjusts during the game)
  int pak(int port) const { return paks_[port]; }  // 1 = Controller Pak, 2 = Rumble Pak
  bool paused_by_host() const { return host_paused_; }
  bool out_of_sync() const { return desynced_; }
  std::string player_name(int id) const;

  // Lockstep, while running.
  uint32_t frame() const { return frame_; }
  // Samples this computer's players for (frame + delay); once per frame, repeated calls are ignored.
  void sample_local(const std::vector<PadState>& local);
  bool ready_to_run() const;
  // True now and then while this computer runs ahead of the others: skip running a frame this time,
  // so the others catch up and nobody waits on anyone's late input.
  bool take_yield();
  PadState input(int port) const;  // for the current frame
  bool reset_due() const { return reset_frame_ == frame_; }
  void frame_done(const uint8_t* ram, std::size_t size);  // after running the current frame
  void flush(bool force = false);  // send sampled input (in pairs of frames, unless forced)
  std::string waiting_for() const;  // who we are waiting on, if the next frame can't run
  // The connection to the relay broke and is being taken up again.
  bool reconnecting() const { return reconnecting_; }
  // For the log: the direct path to each other player.
  std::string direct_status() const;
  void request_reset();             // host
  void set_paused(bool paused);     // host

 private:
  enum Msg : uint8_t {
    kHello = 1, kReject, kLobby, kHaveGame, kStart, kReady, kGo, kInput, kHash, kPing, kPong, kReset, kPause, kEnd,
    kTiming,
    kSeq = 16,     // [seq u32][message]: a numbered message (from the start of a game on)
    kResume = 17,  // [received u32]: the last number received from the other; it sends the rest again
    kBye = 18,     // a guest leaving on purpose
    kDirect = 19,  // this app's direct path offer (direct.hpp)
  };
  // How another computer's inputs arrive here: the margin is how many frames before it was needed
  // each message came (negative: late, the game waited). Measured over a second, then sent to it.
  struct Link {
    int min_margin = INT32_MAX;
    int sum_margin = 0, count = 0;
    double last_avg = 0;         // the last second's average, kept to compare with theirs
    int their_delay = -1;        // what it reported: its own delay,
    int their_min = 0;           // and how early this computer's inputs arrive there
    double their_avg = 0;
    bool reported = false;
  };
  struct Slot {
    uint32_t frame = UINT32_MAX;
    uint8_t known = 0;  // bit per port
    std::array<PadState, kPorts> pads{};
  };

  void open(const std::string& url, const std::string& name);
  void send(int to, const std::string& body);
  void on_text(const std::string& text);
  void on_binary(const std::string& data);
  void handle(const std::string& data);  // a message, its numbering taken off: [from][type][...]
  void on_closed(const net::WebSocket::Event& e);
  std::string url(bool again) const;
  void begin_reconnect(const std::string& why);
  void send_resume(int to);
  void send_offer(int to);
  void pump_direct();
  void player_gone(int id, const std::string& why);
  bool numbered() const { return phase_ == Phase::Starting || phase_ == Phase::Running; }
  void broadcast_lobby();
  void assign_ports();
  void end(const std::string& reason, bool keep_playing);
  void begin();
  void send_timing();
  void on_timing(int from, int their_delay, int min_margin, double avg_margin);
  Player* player(int id);
  Slot& slot(uint32_t frame);
  const Slot* find_slot(uint32_t frame) const;
  uint8_t used_ports() const;

  net::WebSocket ws_;
  net::DirectPath direct_;
  // Taking a broken connection up again: each other player's numbered messages.
  struct Line {
    uint32_t sent = 0, received = 0;
    std::deque<std::string> kept;  // the last messages sent (the newest is number `sent`)
    std::map<uint32_t, std::string> early;  // ones that came ahead of a missing one
    uint64_t gap_ns = 0, asked_ns = 0;
    bool away = false;        // its connection broke; waiting for it to come back
    bool relay_gone = false;  // the relay says it left, but the direct path still reaches it
    bool said_bye = false;
    uint64_t since_ns = 0;
  };
  std::map<int, Line> lines_;
  std::string relay_url_, key_, token_;
  bool reconnecting_ = false;
  uint64_t lost_ns_ = 0, retry_ns_ = 0;
  Phase phase_ = Phase::Off;
  int me_ = -1;
  std::string code_, status_, my_name_, versions_;
  int my_local_players_ = 1;
  uint8_t my_rumble_ = 0;
  std::vector<Player> players_;
  std::array<Port, kPorts> ports_{};
  std::array<int, kPorts> paks_{1, 1, 1, 1};
  GameInfo game_;
  bool game_checked_ = false;
  int delay_ = 4;
  bool delay_chosen_ = false;  // host picked a delay by hand
  uint32_t session_ = 0;
  std::vector<Event> events_;
  uint64_t last_ping_ns_ = 0;
  std::map<int, std::vector<int>> recent_pings_;  // this computer's last few measurements, per player
  uint64_t begin_at_ns_ = 0;  // host: when to start, once the start message has reached the others
  // Adjusting during the game: this computer's own delay, what it learns about each other computer,
  // and frames to hold back while it runs ahead.
  int my_delay_ = 4;
  std::map<int, Link> links_;
  uint64_t last_timing_ns_ = 0, last_lower_ns_ = 0;
  int yield_frames_ = 0;
  uint32_t last_yield_frame_ = 0;

  // Lockstep.
  std::array<Slot, 256> ring_{};
  uint32_t frame_ = 0, sampled_until_ = 0, reset_frame_ = UINT32_MAX;
  std::vector<std::pair<uint32_t, std::vector<PadState>>> pending_;
  std::map<uint32_t, uint64_t> my_hashes_;
  std::map<uint32_t, std::map<int, uint64_t>> their_hashes_;
  bool desynced_ = false;
  bool host_paused_ = false;
};

}  // namespace p64
