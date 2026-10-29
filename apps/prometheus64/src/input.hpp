// Players, devices and the N64 controller each one drives.
//
// Any gamepad joins as the next free player by pressing Start. The keyboard is player 1 when no
// gamepad is connected. "Who's playing" (F10) shows the seats and lets new devices claim the free
// ones: gamepads with any button, the keyboard with Enter, or two keyboard players with F (left
// half) and . (right half). Players can be removed one by one, or all at once to start over.
//
// Every binding can be changed on the Controls screen and is kept in the settings: one set of
// keys for each keyboard layout, and one set of buttons shared by all gamepads.
#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace p64 {

class Settings;

enum N64Button : int {
  kA, kB, kZ, kStart, kL, kR, kCUp, kCDown, kCLeft, kCRight, kDUp, kDDown, kDLeft, kDRight, kButtonCount
};
// Everything a key can drive: the 14 buttons, then the control stick's four directions and the
// walk modifier (keyboard only; a gamepad uses its left stick).
enum Control : int { kStickUp = kButtonCount, kStickDown, kStickLeft, kStickRight, kWalk, kControlCount };

struct PadState {
  uint32_t buttons = 0;  // bit per N64Button
  float x = 0, y = 0;    // control stick, -1..1, y positive = down
};

enum class DeviceKind { None, Keyboard, Gamepad };

// One gamepad input bound to an N64 button: a button, a trigger, or a right-stick direction.
struct PadInput {
  enum Kind : uint8_t { Button, Axis } kind = Button;
  int code = 0;   // SDL_GamepadButton, or SDL_GamepadAxis
  int sign = 1;   // axis direction
  bool operator==(const PadInput&) const = default;
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
  static constexpr int kKeyLayouts = 3;  // whole keyboard, left half, right half

  ~Input();
  void init();
  // Returns true when the event was used to seat a player (so it should not also act in menus).
  bool handle_event(const SDL_Event& e);
  void update(float dt);

  int16_t retro_state(unsigned port, unsigned device, unsigned index, unsigned id) const;
  // This computer's player `seat` as read from its device this frame.
  const PadState& seat_state(int seat) const { return state_[seat]; }
  // Online play: the emulator reads these instead of the local devices.
  void set_overrides(bool on) { overriding_ = on; }
  void set_override(int port, const PadState& pad) { overrides_[port] = pad; }
  // GBA games: the same controls, handed to the core as a GBA's buttons (see retro_state).
  void set_gba(bool on) { gba_ = on; }
  bool rumble(unsigned port, bool strong, uint16_t strength);

  void start_seating();  // "Who's playing": seated players stay, free seats can be claimed
  void stop_seating();   // if nobody is seated, the keyboard takes player 1 again
  void clear_seats();    // "Start over"
  void remove_seat(int player);
  bool seating() const { return seating_; }

  const Seat& seat(int player) const { return seats_[player]; }
  int seated_count() const;
  bool keyboard_shared() const;
  bool has_rumble(int player) const;
  std::vector<std::string> take_notices();
  // Automated tests: hold an N64 button ("A", "START", "CU", "DU", ...) or the stick ("UP", ...).
  void test_press(int player, const std::string& button, bool down);

  // Bindings.
  static const char* control_name(int control);
  static const char* layout_name(int layout);
  SDL_Scancode key(int layout, int control) const { return keys_[layout][control]; }
  std::string key_label(int layout, int control) const;
  std::string pad_label(int button) const;
  // Binds a key; a control that already used it takes this control's old key, so nothing is lost.
  void bind_key(int layout, int control, SDL_Scancode key);
  // Binds the gamepad input in `e` (a button press, a trigger pulled or the right stick pushed).
  // Returns false when the event is not something that can be bound.
  bool bind_pad(int button, const SDL_Event& e);
  void reset_keys(int layout);
  void reset_pad();
  void load_bindings(const Settings& settings);
  void save_bindings(Settings& settings) const;

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
  bool pad_input_held(SDL_Gamepad* g, const PadInput& in) const;

  std::vector<Pad> pads_;
  std::array<Seat, kPlayers> seats_{};
  std::array<PadState, kPlayers> state_{};
  std::array<float, kPlayers> kb_x_{}, kb_y_{};  // smoothed keyboard stick
  std::array<uint16_t, kPlayers> rumble_strong_{}, rumble_weak_{};
  std::array<uint32_t, kPlayers> test_buttons_{};
  std::array<float, kPlayers> test_x_{}, test_y_{};
  std::array<std::array<SDL_Scancode, kControlCount>, kKeyLayouts> keys_{};
  std::array<std::vector<PadInput>, kButtonCount> pad_{};
  bool seating_ = false;
  bool overriding_ = false;
  std::array<PadState, kPlayers> overrides_{};
  bool gba_ = false;
  std::vector<std::string> notices_;
};

}  // namespace p64
