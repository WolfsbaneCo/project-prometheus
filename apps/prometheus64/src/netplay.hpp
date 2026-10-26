// Online play, lockstep: every computer runs the same game from the same start, and only
// controller inputs travel (through the relay). Each computer samples its own players a few
// frames ahead (the input delay) and sends that input to the others; a frame runs only once the
// input of every player for it has arrived, so every copy of the game stays identical. Every few
// seconds the computers compare a fingerprint of the console's memory to catch any drift.
//
// The host picks the game, the input delay and which computer drives which N64 port, sends its
// own save to the guests, and alone can pause or reset. Brief section 11.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "input.hpp"
#include "net.hpp"

namespace p64 {

class Netplay {
 public:
  static constexpr int kProtocol = 1;  // this app's own messages (the relay has its own version)
  static constexpr int kPorts = 4;
  static constexpr int kMinDelay = 2, kMaxDelay = 12;

  enum class Phase { Off, Connecting, Lobby, Starting, Running };

  struct GameInfo {
    std::string id, title;
    std::array<uint8_t, 32> sha{};  // SHA-256 of the game file: everyone needs the identical game
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
  int delay() const { return delay_; }
  int pak(int port) const { return paks_[port]; }  // 1 = Controller Pak, 2 = Rumble Pak
  bool paused_by_host() const { return host_paused_; }
  bool out_of_sync() const { return desynced_; }
  std::string player_name(int id) const;

  // Lockstep, while running.
  uint32_t frame() const { return frame_; }
  // Samples this computer's players for (frame + delay); once per frame, repeated calls are ignored.
  void sample_local(const std::vector<PadState>& local);
  bool ready_to_run() const;
  PadState input(int port) const;  // for the current frame
  bool reset_due() const { return reset_frame_ == frame_; }
  void frame_done(const uint8_t* ram, std::size_t size);  // after running the current frame
  void flush(bool force = false);  // send sampled input (in pairs of frames, unless forced)
  std::string waiting_for() const;  // who we are waiting on, if the next frame can't run
  void request_reset();             // host
  void set_paused(bool paused);     // host

 private:
  enum Msg : uint8_t {
    kHello = 1, kReject, kLobby, kHaveGame, kStart, kReady, kGo, kInput, kHash, kPing, kPong, kReset, kPause, kEnd
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
  void on_closed(const std::string& reason);
  void broadcast_lobby();
  void assign_ports();
  void end(const std::string& reason, bool keep_playing);
  void begin();
  Player* player(int id);
  Slot& slot(uint32_t frame);
  const Slot* find_slot(uint32_t frame) const;
  uint8_t used_ports() const;

  net::WebSocket ws_;
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
