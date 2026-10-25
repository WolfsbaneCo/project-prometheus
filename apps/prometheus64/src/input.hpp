// Players, devices and the N64 controller each one drives.
//
// Any gamepad joins as the next free player by pressing Start. The keyboard is player 1 when no
// gamepad is connected. "Who's playing" (F10) clears the seats and lets every device claim one:
// gamepads with any button, the keyboard with Enter, or two keyboard players with F (left half)
// and . (right half).
#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace p64 {

enum N64Button : int {
  kA, kB, kZ, kStart, kL, kR, kCUp, kCDown, kCLeft, kCRight, kDUp, kDDown, kDLeft, kDRight, kButtonCount
};

struct PadState {
  uint32_t buttons = 0;  // bit per N64Button
  float x = 0, y = 0;    // control stick, -1..1, y positive = down
};

enum class DeviceKind { None, Keyboard, Gamepad };

// Which keys drive one keyboard player.
struct KeyLayout {
  const char* name;
  SDL_Scancode up, down, left, right, walk;
  std::array<SDL_Scancode, kButtonCount> buttons;
};

struct Seat {
  DeviceKind kind = DeviceKind::None;
  SDL_JoystickID gamepad = 0;
  int layout = 0;  // keyboard layout index
  std::string name;
  bool empty() const { return kind == DeviceKind::None; }
};

class Input {
 public:
  static constexpr int kPlayers = 4;

  ~Input();
  void init();
  // Returns true when the event was used to seat a player (so it should not also act in menus).
  bool handle_event(const SDL_Event& e);
  void update(float dt);

  int16_t retro_state(unsigned port, unsigned device, unsigned index, unsigned id) const;
  bool rumble(unsigned port, bool strong, uint16_t strength);

  void start_seating();  // "Who's playing": clears every seat
  void stop_seating() { seating_ = false; }
  bool seating() const { return seating_; }

  const Seat& seat(int player) const { return seats_[player]; }
  int seated_count() const;
  bool keyboard_shared() const;
  bool has_rumble(int player) const;
  std::vector<std::string> take_notices();
  // Automated tests: hold an N64 button ("A", "START", "CU", "DU", ...) or the stick ("UP", ...).
  void test_press(int player, const std::string& button, bool down);
  static const KeyLayout& layout(int index);
  static int layout_count();

 private:
  struct Pad {
    SDL_JoystickID id;
    SDL_Gamepad* handle;
  };
  SDL_Gamepad* gamepad(SDL_JoystickID id) const;
  int seat_of_gamepad(SDL_JoystickID id) const;
  int seat_of_layout(int layout) const;
  int first_free() const;
  void seat_device(int player, Seat seat);
  void auto_seat_keyboard();

  std::vector<Pad> pads_;
  std::array<Seat, kPlayers> seats_{};
  std::array<PadState, kPlayers> state_{};
  std::array<float, kPlayers> kb_x_{}, kb_y_{};  // smoothed keyboard stick
  std::array<uint16_t, kPlayers> rumble_strong_{}, rumble_weak_{};
  std::array<uint32_t, kPlayers> test_buttons_{};
  std::array<float, kPlayers> test_x_{}, test_y_{};
  bool seating_ = false;
  std::vector<std::string> notices_;
};

}  // namespace p64
