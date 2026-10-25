#include "input.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "libretro.h"

namespace p64 {
namespace {

// Keyboard layouts. Layout 0 uses the whole keyboard (one player); 1 and 2 split it in two.
// Button order follows N64Button: A, B, Z, Start, L, R, C-Up, C-Down, C-Left, C-Right, D-pad x4.
const KeyLayout kLayouts[] = {
    {"Keyboard", SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_LSHIFT,
     {SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_Z, SDL_SCANCODE_RETURN, SDL_SCANCODE_A, SDL_SCANCODE_S,
      SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_T, SDL_SCANCODE_G, SDL_SCANCODE_F,
      SDL_SCANCODE_H}},
    {"Keyboard (left side)", SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_LSHIFT,
     {SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_Q, SDL_SCANCODE_TAB, SDL_SCANCODE_R, SDL_SCANCODE_E,
      SDL_SCANCODE_T, SDL_SCANCODE_B, SDL_SCANCODE_V, SDL_SCANCODE_N, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
      SDL_SCANCODE_4}},
    {"Keyboard (right side)", SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
     SDL_SCANCODE_RSHIFT,
     {SDL_SCANCODE_PERIOD, SDL_SCANCODE_COMMA, SDL_SCANCODE_M, SDL_SCANCODE_RETURN, SDL_SCANCODE_O,
      SDL_SCANCODE_SLASH, SDL_SCANCODE_P, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_L, SDL_SCANCODE_APOSTROPHE,
      SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_DELETE, SDL_SCANCODE_PAGEDOWN}},
};

// The core's "independent C-buttons" mapping (option alt-map = True): N64 button -> RetroPad bit.
const unsigned kRetroBit[kButtonCount] = {
    RETRO_DEVICE_ID_JOYPAD_B,     RETRO_DEVICE_ID_JOYPAD_Y,  RETRO_DEVICE_ID_JOYPAD_L2,
    RETRO_DEVICE_ID_JOYPAD_START, RETRO_DEVICE_ID_JOYPAD_SELECT, RETRO_DEVICE_ID_JOYPAD_R2,
    RETRO_DEVICE_ID_JOYPAD_X,     RETRO_DEVICE_ID_JOYPAD_A,  RETRO_DEVICE_ID_JOYPAD_L,
    RETRO_DEVICE_ID_JOYPAD_R,     RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN,
    RETRO_DEVICE_ID_JOYPAD_LEFT,  RETRO_DEVICE_ID_JOYPAD_RIGHT,
};

float axis(SDL_Gamepad* pad, SDL_GamepadAxis a) {
  return std::clamp(SDL_GetGamepadAxis(pad, a) / 32767.0f, -1.0f, 1.0f);
}

std::string player_label(int i) { return "Player " + std::to_string(i + 1); }

}  // namespace

const KeyLayout& Input::layout(int index) { return kLayouts[index]; }
int Input::layout_count() { return static_cast<int>(std::size(kLayouts)); }

Input::~Input() {
  for (auto& p : pads_) SDL_CloseGamepad(p.handle);
}

void Input::init() {
  int count = 0;
  if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
    for (int i = 0; i < count; ++i) {
      if (SDL_Gamepad* g = SDL_OpenGamepad(ids[i])) pads_.push_back({ids[i], g});
    }
    SDL_free(ids);
  }
  // Seat connected gamepads in order; the keyboard takes player 1 only if there are none.
  for (auto& p : pads_) {
    if (first_free() < 0) break;
    seat_device(first_free(), {DeviceKind::Gamepad, p.id, 0, SDL_GetGamepadName(p.handle) ? SDL_GetGamepadName(p.handle) : "Gamepad"});
  }
  auto_seat_keyboard();
}

void Input::auto_seat_keyboard() {
  if (seated_count() == 0) seat_device(0, {DeviceKind::Keyboard, 0, 0, kLayouts[0].name});
}

SDL_Gamepad* Input::gamepad(SDL_JoystickID id) const {
  for (auto& p : pads_) {
    if (p.id == id) return p.handle;
  }
  return nullptr;
}

int Input::seat_of_gamepad(SDL_JoystickID id) const {
  for (int i = 0; i < kPlayers; ++i) {
    if (seats_[i].kind == DeviceKind::Gamepad && seats_[i].gamepad == id) return i;
  }
  return -1;
}

int Input::seat_of_layout(int layout) const {
  for (int i = 0; i < kPlayers; ++i) {
    if (seats_[i].kind == DeviceKind::Keyboard && seats_[i].layout == layout) return i;
  }
  return -1;
}

int Input::first_free() const {
  for (int i = 0; i < kPlayers; ++i) {
    if (seats_[i].empty()) return i;
  }
  return -1;
}

int Input::seated_count() const {
  return static_cast<int>(std::count_if(seats_.begin(), seats_.end(), [](const Seat& s) { return !s.empty(); }));
}

bool Input::keyboard_shared() const {
  int n = 0;
  for (const auto& s : seats_) n += s.kind == DeviceKind::Keyboard ? 1 : 0;
  return n > 1;
}

bool Input::has_rumble(int player) const {
  const Seat& s = seats_[player];
  if (s.kind != DeviceKind::Gamepad) return false;
  SDL_Gamepad* g = gamepad(s.gamepad);
  return g && SDL_GetBooleanProperty(SDL_GetGamepadProperties(g), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false);
}

void Input::seat_device(int player, Seat seat) {
  seats_[player] = seat;
  state_[player] = {};
  notices_.push_back(player_label(player) + ": " + seat.name);
}

void Input::start_seating() {
  seating_ = true;
  for (auto& s : seats_) s = {};
  for (auto& st : state_) st = {};
  notices_.push_back("Who's playing? Controllers: press any button. Keyboard: Enter (F = left half, . = right half)");
}

bool Input::handle_event(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!gamepad(e.gdevice.which)) {
        if (SDL_Gamepad* g = SDL_OpenGamepad(e.gdevice.which)) {
          pads_.push_back({e.gdevice.which, g});
          notices_.push_back(std::string(SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "Gamepad") +
                             " connected. Press Start to join");
        }
      }
      return false;
    case SDL_EVENT_GAMEPAD_REMOVED: {
      const int seat = seat_of_gamepad(e.gdevice.which);
      if (seat >= 0) {
        notices_.push_back(player_label(seat) + "'s controller was disconnected");
        seats_[seat] = {};
        state_[seat] = {};
      }
      auto it = std::find_if(pads_.begin(), pads_.end(), [&](const Pad& p) { return p.id == e.gdevice.which; });
      if (it != pads_.end()) {
        SDL_CloseGamepad(it->handle);
        pads_.erase(it);
      }
      if (!seating_) auto_seat_keyboard();
      return false;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
      const bool claims = seating_ || e.gbutton.button == SDL_GAMEPAD_BUTTON_START;
      if (claims && seat_of_gamepad(e.gbutton.which) < 0 && first_free() >= 0) {
        SDL_Gamepad* g = gamepad(e.gbutton.which);
        // A gamepad joining takes over player 1 from a keyboard nobody asked for.
        int seat = first_free();
        if (!seating_ && seated_count() == 1 && seats_[0].kind == DeviceKind::Keyboard && seats_[0].layout == 0) seat = 0;
        seat_device(seat, {DeviceKind::Gamepad, e.gbutton.which, 0, g && SDL_GetGamepadName(g) ? SDL_GetGamepadName(g) : "Gamepad"});
        return true;
      }
      return false;
    }
    case SDL_EVENT_KEY_DOWN: {
      if (!seating_ || e.key.repeat || first_free() < 0) return false;
      // Explicit join keys, so one person's keys are never mistaken for a second player:
      // Enter = the keyboard player (or the right half if the left is taken),
      // F = left half, . (period) = right half.
      int layout = -1;
      if (e.key.scancode == SDL_SCANCODE_RETURN) {
        layout = seat_of_layout(1) >= 0 ? 2 : 0;
      } else if (e.key.scancode == SDL_SCANCODE_F) {
        layout = 1;
      } else if (e.key.scancode == SDL_SCANCODE_PERIOD) {
        layout = 2;
      }
      if (layout < 0 || seat_of_layout(layout) >= 0) return false;
      if (layout == 0 && (seat_of_layout(1) >= 0 || seat_of_layout(2) >= 0)) return false;
      if (layout != 0 && seat_of_layout(0) >= 0) {
        // The whole-keyboard player moves to the other half so two people can share it.
        const int other = seat_of_layout(0);
        seats_[other].layout = layout == 1 ? 2 : 1;
        seats_[other].name = kLayouts[seats_[other].layout].name;
        notices_.push_back(player_label(other) + " now uses the " +
                           (seats_[other].layout == 1 ? "left" : "right") + " side of the keyboard");
      }
      seat_device(first_free(), {DeviceKind::Keyboard, 0, layout, kLayouts[layout].name});
      if (keyboard_shared()) {
        notices_.push_back("Two players on one keyboard: many keyboards miss keys pressed together. Gamepads work better");
      }
      return true;
    }
    default:
      return false;
  }
}

void Input::update(float dt) {
  const bool* keys = SDL_GetKeyboardState(nullptr);
  for (int p = 0; p < kPlayers; ++p) {
    const Seat& s = seats_[p];
    PadState st;
    if (s.kind == DeviceKind::Keyboard) {
      const KeyLayout& l = kLayouts[s.layout];
      for (int b = 0; b < kButtonCount; ++b) {
        if (keys[l.buttons[b]]) st.buttons |= 1u << b;
      }
      // Keys give all-or-nothing input. Ease toward the target so turns are smooth, and let the
      // walk key halve the stick for walking speed.
      float tx = (keys[l.right] ? 1.0f : 0.0f) - (keys[l.left] ? 1.0f : 0.0f);
      float ty = (keys[l.down] ? 1.0f : 0.0f) - (keys[l.up] ? 1.0f : 0.0f);
      const float len = std::hypot(tx, ty);
      if (len > 1.0f) {
        tx /= len;
        ty /= len;
      }
      if (keys[l.walk]) {
        tx *= 0.5f;
        ty *= 0.5f;
      }
      const float k = std::min(1.0f, dt / 0.06f);
      kb_x_[p] += (tx - kb_x_[p]) * k;
      kb_y_[p] += (ty - kb_y_[p]) * k;
      st.x = kb_x_[p];
      st.y = kb_y_[p];
    } else if (s.kind == DeviceKind::Gamepad) {
      SDL_Gamepad* g = gamepad(s.gamepad);
      if (!g) continue;
      auto down = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(g, b); };
      auto set = [&](int b, bool on) {
        if (on) st.buttons |= 1u << b;
      };
      set(kA, down(SDL_GAMEPAD_BUTTON_SOUTH));
      set(kB, down(SDL_GAMEPAD_BUTTON_WEST));
      set(kCDown, down(SDL_GAMEPAD_BUTTON_EAST));
      set(kCLeft, down(SDL_GAMEPAD_BUTTON_NORTH));
      set(kStart, down(SDL_GAMEPAD_BUTTON_START));
      set(kL, down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
      set(kR, down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
      set(kZ, axis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 0.4f || axis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.4f);
      set(kDUp, down(SDL_GAMEPAD_BUTTON_DPAD_UP));
      set(kDDown, down(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
      set(kDLeft, down(SDL_GAMEPAD_BUTTON_DPAD_LEFT));
      set(kDRight, down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
      const float rx = axis(g, SDL_GAMEPAD_AXIS_RIGHTX), ry = axis(g, SDL_GAMEPAD_AXIS_RIGHTY);
      set(kCRight, rx > 0.5f);
      set(kCLeft, rx < -0.5f);
      set(kCDown, ry > 0.5f);
      set(kCUp, ry < -0.5f);
      st.x = axis(g, SDL_GAMEPAD_AXIS_LEFTX);
      st.y = axis(g, SDL_GAMEPAD_AXIS_LEFTY);
    }
    st.buttons |= test_buttons_[p];
    if (test_x_[p] != 0 || test_y_[p] != 0) {
      st.x = test_x_[p];
      st.y = test_y_[p];
    }
    state_[p] = st;
  }
}

void Input::test_press(int player, const std::string& button, bool down) {
  if (player < 0 || player >= kPlayers) return;
  static const char* names[kButtonCount] = {"A", "B", "Z", "START", "L", "R", "CU", "CD", "CL", "CR", "DU", "DD", "DL", "DR"};
  for (int b = 0; b < kButtonCount; ++b) {
    if (button == names[b]) {
      if (down) {
        test_buttons_[player] |= 1u << b;
      } else {
        test_buttons_[player] &= ~(1u << b);
      }
      return;
    }
  }
  const float v = down ? 1.0f : 0.0f;
  if (button == "UP") test_y_[player] = -v;
  if (button == "DOWN") test_y_[player] = v;
  if (button == "LEFT") test_x_[player] = -v;
  if (button == "RIGHT") test_x_[player] = v;
}

int16_t Input::retro_state(unsigned port, unsigned device, unsigned index, unsigned id) const {
  if (port >= kPlayers) return 0;
  const PadState& st = state_[port];
  if (device == RETRO_DEVICE_JOYPAD) {
    uint16_t mask = 0;
    for (int b = 0; b < kButtonCount; ++b) {
      if (st.buttons & (1u << b)) mask |= static_cast<uint16_t>(1u << kRetroBit[b]);
    }
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(mask);
    return (mask >> id) & 1 ? 1 : 0;
  }
  if (device == RETRO_DEVICE_ANALOG && index == RETRO_DEVICE_INDEX_ANALOG_LEFT) {
    const float v = id == RETRO_DEVICE_ID_ANALOG_X ? st.x : st.y;
    return static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f));
  }
  return 0;  // C buttons arrive as buttons, not on the right stick
}

bool Input::rumble(unsigned port, bool strong, uint16_t strength) {
  if (port >= kPlayers || seats_[port].kind != DeviceKind::Gamepad) return false;
  SDL_Gamepad* g = gamepad(seats_[port].gamepad);
  if (!g) return false;
  (strong ? rumble_strong_ : rumble_weak_)[port] = strength;
  return SDL_RumbleGamepad(g, rumble_strong_[port], rumble_weak_[port], 5000);
}

std::vector<std::string> Input::take_notices() {
  std::vector<std::string> out;
  out.swap(notices_);
  return out;
}

}  // namespace p64
