#include "input.hpp"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "libretro.h"
#include "settings.hpp"

namespace p64 {
namespace {

// Default keys for each keyboard layout. Layout 0 uses the whole keyboard (one player); 1 and 2
// split it in two. Order follows Control: A, B, Z, Start, L, R, C-Up, C-Down, C-Left, C-Right,
// D-pad up/down/left/right, then stick up/down/left/right and walk.
using KeySet = std::array<SDL_Scancode, kControlCount>;
const KeySet kDefaultKeys[Input::kKeyLayouts] = {
    {SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_Z, SDL_SCANCODE_RETURN, SDL_SCANCODE_A, SDL_SCANCODE_S,
     SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L, SDL_SCANCODE_T, SDL_SCANCODE_G, SDL_SCANCODE_F,
     SDL_SCANCODE_H, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_LSHIFT},
    {SDL_SCANCODE_F, SDL_SCANCODE_G, SDL_SCANCODE_Q, SDL_SCANCODE_TAB, SDL_SCANCODE_R, SDL_SCANCODE_E,
     SDL_SCANCODE_T, SDL_SCANCODE_B, SDL_SCANCODE_V, SDL_SCANCODE_N, SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
     SDL_SCANCODE_4, SDL_SCANCODE_W, SDL_SCANCODE_S, SDL_SCANCODE_A, SDL_SCANCODE_D, SDL_SCANCODE_LSHIFT},
    {SDL_SCANCODE_PERIOD, SDL_SCANCODE_COMMA, SDL_SCANCODE_M, SDL_SCANCODE_RETURN, SDL_SCANCODE_O,
     SDL_SCANCODE_SLASH, SDL_SCANCODE_P, SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_L, SDL_SCANCODE_APOSTROPHE,
     SDL_SCANCODE_HOME, SDL_SCANCODE_END, SDL_SCANCODE_DELETE, SDL_SCANCODE_PAGEDOWN, SDL_SCANCODE_UP,
     SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_RSHIFT},
};
const char* const kLayoutNames[Input::kKeyLayouts] = {"Keyboard", "Keyboard (left side)", "Keyboard (right side)"};

// Default gamepad buttons. The right stick also gives the C buttons, and either trigger is Z.
std::array<std::vector<PadInput>, kButtonCount> default_pad() {
  auto b = [](SDL_GamepadButton x) { return PadInput{PadInput::Button, x, 1}; };
  auto a = [](SDL_GamepadAxis x, int sign) { return PadInput{PadInput::Axis, x, sign}; };
  std::array<std::vector<PadInput>, kButtonCount> p;
  p[kA] = {b(SDL_GAMEPAD_BUTTON_SOUTH)};
  p[kB] = {b(SDL_GAMEPAD_BUTTON_WEST)};
  p[kZ] = {a(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), a(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1)};
  p[kStart] = {b(SDL_GAMEPAD_BUTTON_START)};
  p[kL] = {b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)};
  p[kR] = {b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)};
  p[kCUp] = {a(SDL_GAMEPAD_AXIS_RIGHTY, -1)};
  p[kCDown] = {b(SDL_GAMEPAD_BUTTON_EAST), a(SDL_GAMEPAD_AXIS_RIGHTY, 1)};
  p[kCLeft] = {b(SDL_GAMEPAD_BUTTON_NORTH), a(SDL_GAMEPAD_AXIS_RIGHTX, -1)};
  p[kCRight] = {a(SDL_GAMEPAD_AXIS_RIGHTX, 1)};
  p[kDUp] = {b(SDL_GAMEPAD_BUTTON_DPAD_UP)};
  p[kDDown] = {b(SDL_GAMEPAD_BUTTON_DPAD_DOWN)};
  p[kDLeft] = {b(SDL_GAMEPAD_BUTTON_DPAD_LEFT)};
  p[kDRight] = {b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)};
  return p;
}

// Thresholds: a trigger counts as pulled, the right stick as pushed (for the C buttons).
constexpr float kTriggerOn = 0.4f, kStickOn = 0.5f, kCaptureOn = 0.6f;

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

Input::~Input() {
  for (auto& p : pads_) SDL_CloseGamepad(p.handle);
}

void Input::init() {
  for (int l = 0; l < kKeyLayouts; ++l) keys_[l] = kDefaultKeys[l];
  pad_ = default_pad();
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
  if (seated_count() == 0) seat_device(0, {DeviceKind::Keyboard, 0, 0, kLayoutNames[0]});
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

void Input::start_seating() { seating_ = true; }

void Input::stop_seating() {
  seating_ = false;
  auto_seat_keyboard();
}

void Input::clear_seats() {
  for (auto& seat : seats_) seat = {};
  for (auto& st : state_) st = {};
}

void Input::remove_seat(int player) {
  if (player < 0 || player >= kPlayers) return;
  seats_[player] = {};
  state_[player] = {};
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
        seats_[other].name = kLayoutNames[seats_[other].layout];
        notices_.push_back(player_label(other) + " now uses the " +
                           (seats_[other].layout == 1 ? "left" : "right") + " side of the keyboard");
      }
      seat_device(first_free(), {DeviceKind::Keyboard, 0, layout, kLayoutNames[layout]});
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
      const KeySet& l = keys_[s.layout];
      for (int b = 0; b < kButtonCount; ++b) {
        if (keys[l[b]]) st.buttons |= 1u << b;
      }
      // Keys give all-or-nothing input. Ease toward the target so turns are smooth, and let the
      // walk key halve the stick for walking speed.
      float tx = (keys[l[kStickRight]] ? 1.0f : 0.0f) - (keys[l[kStickLeft]] ? 1.0f : 0.0f);
      float ty = (keys[l[kStickDown]] ? 1.0f : 0.0f) - (keys[l[kStickUp]] ? 1.0f : 0.0f);
      const float len = std::hypot(tx, ty);
      if (len > 1.0f) {
        tx /= len;
        ty /= len;
      }
      if (keys[l[kWalk]]) {
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
      for (int b = 0; b < kButtonCount; ++b) {
        for (const PadInput& in : pad_[b]) {
          if (pad_input_held(g, in)) st.buttons |= 1u << b;
        }
      }
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
  const PadState& st = overriding_ ? overrides_[port] : state_[port];
  if (gba_) {
    // A GBA: A and B, L and R, Start as on the N64 controls; Select on Z (the keyboard's Z, a
    // controller's triggers); the +Control Pad on the D-pad and on the stick (the arrow keys).
    if (device != RETRO_DEVICE_JOYPAD) return 0;
    auto on = [&](int b) { return (st.buttons & (1u << b)) != 0; };
    uint16_t mask = 0;
    auto put = [&](unsigned id, bool v) { if (v) mask |= static_cast<uint16_t>(1u << id); };
    put(RETRO_DEVICE_ID_JOYPAD_A, on(kA));
    put(RETRO_DEVICE_ID_JOYPAD_B, on(kB));
    put(RETRO_DEVICE_ID_JOYPAD_L, on(kL));
    put(RETRO_DEVICE_ID_JOYPAD_R, on(kR));
    put(RETRO_DEVICE_ID_JOYPAD_START, on(kStart));
    put(RETRO_DEVICE_ID_JOYPAD_SELECT, on(kZ));
    put(RETRO_DEVICE_ID_JOYPAD_UP, on(kDUp) || st.y < -0.3f);
    put(RETRO_DEVICE_ID_JOYPAD_DOWN, on(kDDown) || st.y > 0.3f);
    put(RETRO_DEVICE_ID_JOYPAD_LEFT, on(kDLeft) || st.x < -0.3f);
    put(RETRO_DEVICE_ID_JOYPAD_RIGHT, on(kDRight) || st.x > 0.3f);
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(mask);
    return (mask >> id) & 1 ? 1 : 0;
  }
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

// ---------------------------------------------------------------------------------------------
// Bindings

namespace {

// Names used in the settings file; they must never change.
const char* const kControlIds[kControlCount] = {"a",   "b",     "z",     "start", "l",  "r",    "cup",
                                                "cdown", "cleft", "cright", "dup", "ddown", "dleft", "dright",
                                                "up",  "down",  "left",  "right", "walk"};

std::string encode(const std::vector<PadInput>& list) {
  std::string out;
  for (const PadInput& in : list) {
    if (!out.empty()) out += ',';
    out += (in.kind == PadInput::Button ? "b" : "a") + std::to_string(in.code);
    if (in.kind == PadInput::Axis) out += in.sign > 0 ? '+' : '-';
  }
  return out;
}

bool decode(const std::string& text, std::vector<PadInput>& out) {
  out.clear();
  std::size_t at = 0;
  while (at < text.size()) {
    std::size_t end = text.find(',', at);
    if (end == std::string::npos) end = text.size();
    const std::string t = text.substr(at, end - at);
    at = end + 1;
    if (t.size() < 2 || (t[0] != 'b' && t[0] != 'a')) return false;
    PadInput in;
    in.kind = t[0] == 'b' ? PadInput::Button : PadInput::Axis;
    const bool signed_axis = in.kind == PadInput::Axis && (t.back() == '+' || t.back() == '-');
    if (in.kind == PadInput::Axis && !signed_axis) return false;
    try {
      in.code = std::stoi(t.substr(1, t.size() - 1 - (signed_axis ? 1 : 0)));
    } catch (...) {
      return false;
    }
    if (signed_axis) in.sign = t.back() == '+' ? 1 : -1;
    const int limit = in.kind == PadInput::Button ? static_cast<int>(SDL_GAMEPAD_BUTTON_COUNT)
                                                  : static_cast<int>(SDL_GAMEPAD_AXIS_COUNT);
    if (in.code < 0 || in.code >= limit) return false;
    out.push_back(in);
  }
  return !out.empty();
}

const char* face_label(SDL_GamepadButtonLabel l) {
  switch (l) {
    case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
    case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
    case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
    case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
    default: return nullptr;
  }
}

}  // namespace

const char* Input::control_name(int control) {
  static const char* const names[kControlCount] = {
      "A",        "B",          "Z",          "Start",       "L",          "R",           "C-Up",
      "C-Down",   "C-Left",     "C-Right",    "D-pad up",    "D-pad down", "D-pad left",  "D-pad right",
      "Stick up", "Stick down", "Stick left", "Stick right", "Walk (hold)"};
  return control >= 0 && control < kControlCount ? names[control] : "";
}

const char* Input::layout_name(int layout) { return kLayoutNames[layout]; }

std::string Input::key_label(int layout, int control) const {
  const SDL_Scancode sc = keys_[layout][control];
  switch (sc) {
    case SDL_SCANCODE_UNKNOWN: return "None";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_UP: return "Up arrow";
    case SDL_SCANCODE_DOWN: return "Down arrow";
    case SDL_SCANCODE_LEFT: return "Left arrow";
    case SDL_SCANCODE_RIGHT: return "Right arrow";
    default: break;
  }
  const char* name = SDL_GetScancodeName(sc);
  return name && *name ? name : "Key " + std::to_string(static_cast<int>(sc));
}

std::string Input::pad_label(int button) const {
  SDL_Gamepad* any = pads_.empty() ? nullptr : pads_.front().handle;
  auto one = [&](const PadInput& in) -> std::string {
    if (in.kind == PadInput::Axis) {
      switch (in.code) {
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: return "LT";
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return "RT";
        case SDL_GAMEPAD_AXIS_RIGHTX: return in.sign > 0 ? "Right stick right" : "Right stick left";
        case SDL_GAMEPAD_AXIS_RIGHTY: return in.sign > 0 ? "Right stick down" : "Right stick up";
        default: return "Left stick";
      }
    }
    const auto b = static_cast<SDL_GamepadButton>(in.code);
    const SDL_GamepadButtonLabel l = any ? SDL_GetGamepadButtonLabel(any, b)
                                         : SDL_GetGamepadButtonLabelForType(SDL_GAMEPAD_TYPE_XBOXONE, b);
    if (const char* f = face_label(l)) return f;
    switch (b) {
      case SDL_GAMEPAD_BUTTON_BACK: return "Back";
      case SDL_GAMEPAD_BUTTON_GUIDE: return "Guide";
      case SDL_GAMEPAD_BUTTON_START: return "Start";
      case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "Left stick press";
      case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "Right stick press";
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
      case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad up";
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad down";
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad left";
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad right";
      case SDL_GAMEPAD_BUTTON_MISC1: return "Share";
      case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
      default: return "Button " + std::to_string(in.code);
    }
  };
  std::string out;
  for (const PadInput& in : pad_[button]) out += (out.empty() ? "" : " or ") + one(in);
  return out.empty() ? "None" : out;
}

void Input::bind_key(int layout, int control, SDL_Scancode key) {
  KeySet& set = keys_[layout];
  const SDL_Scancode old = set[control];
  for (int c = 0; c < kControlCount; ++c) {
    if (c != control && set[c] == key) set[c] = old;
  }
  set[control] = key;
}

bool Input::bind_pad(int button, const SDL_Event& e) {
  PadInput in;
  if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    in = {PadInput::Button, e.gbutton.button, 1};
  } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
    const float v = e.gaxis.value / 32767.0f;
    const auto a = static_cast<SDL_GamepadAxis>(e.gaxis.axis);
    if (a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
      if (v < kCaptureOn) return false;
      in = {PadInput::Axis, a, 1};
    } else if (a == SDL_GAMEPAD_AXIS_RIGHTX || a == SDL_GAMEPAD_AXIS_RIGHTY) {
      if (std::fabs(v) < kCaptureOn) return false;
      in = {PadInput::Axis, a, v > 0 ? 1 : -1};
    } else {
      return false;  // the left stick is the control stick
    }
  } else {
    return false;
  }
  const std::vector<PadInput> old = pad_[button];
  for (int b = 0; b < kButtonCount; ++b) {
    if (b == button) continue;
    auto& list = pad_[b];
    const auto before = list.size();
    list.erase(std::remove(list.begin(), list.end(), in), list.end());
    if (list.empty() && before > 0) {
      for (const PadInput& o : old) {
        if (!(o == in)) list.push_back(o);
      }
    }
  }
  pad_[button] = {in};
  return true;
}

void Input::reset_keys(int layout) { keys_[layout] = kDefaultKeys[layout]; }

void Input::reset_pad() { pad_ = default_pad(); }

bool Input::pad_input_held(SDL_Gamepad* g, const PadInput& in) const {
  if (in.kind == PadInput::Button) return SDL_GetGamepadButton(g, static_cast<SDL_GamepadButton>(in.code));
  const auto a = static_cast<SDL_GamepadAxis>(in.code);
  const bool trigger = a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
  return axis(g, a) * static_cast<float>(in.sign) > (trigger ? kTriggerOn : kStickOn);
}

void Input::load_bindings(const Settings& settings) {
  for (int l = 0; l < kKeyLayouts; ++l) {
    keys_[l] = kDefaultKeys[l];
    for (int c = 0; c < kControlCount; ++c) {
      const int v = settings.get_int("keys." + std::to_string(l) + "." + kControlIds[c], -1);
      if (v > 0 && v < SDL_SCANCODE_COUNT) keys_[l][c] = static_cast<SDL_Scancode>(v);
    }
  }
  pad_ = default_pad();
  for (int b = 0; b < kButtonCount; ++b) {
    std::vector<PadInput> list;
    if (decode(settings.get(std::string("pad.") + kControlIds[b]), list)) pad_[b] = list;
  }
}

void Input::save_bindings(Settings& settings) const {
  for (int l = 0; l < kKeyLayouts; ++l) {
    for (int c = 0; c < kControlCount; ++c) {
      settings.set_int("keys." + std::to_string(l) + "." + kControlIds[c], static_cast<int>(keys_[l][c]));
    }
  }
  for (int b = 0; b < kButtonCount; ++b) settings.set(std::string("pad.") + kControlIds[b], encode(pad_[b]));
}

}  // namespace p64
