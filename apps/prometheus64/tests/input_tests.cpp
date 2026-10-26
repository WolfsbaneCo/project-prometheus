// Controls screen bindings: rebinding, swapping, saving and loading, without any device attached.
#include <SDL3/SDL.h>

#include <cstdio>
#include <filesystem>

#include "input.hpp"
#include "settings.hpp"
using namespace p64;
static SDL_Event btn(SDL_GamepadButton b) { SDL_Event e{}; e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN; e.gbutton.button = (Uint8)b; e.gbutton.down = true; return e; }
static SDL_Event ax(SDL_GamepadAxis a, int v) { SDL_Event e{}; e.type = SDL_EVENT_GAMEPAD_AXIS_MOTION; e.gaxis.axis = (Uint8)a; e.gaxis.value = (Sint16)v; return e; }
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
int main() {
  Input in; in.init();
  CHECK(in.pad_label(kA) == "A"); CHECK(in.pad_label(kZ) == "LT or RT");
  // Bind A to Y (North): C-Left loses Y but keeps the right stick.
  CHECK(in.bind_pad(kA, btn(SDL_GAMEPAD_BUTTON_NORTH)));
  CHECK(in.pad_label(kA) == "Y"); CHECK(in.pad_label(kCLeft) == "Right stick left");
  // Bind B to RB: R loses its only input, so it takes B's old one (X).
  CHECK(in.bind_pad(kB, btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
  CHECK(in.pad_label(kB) == "RB"); CHECK(in.pad_label(kR) == "X");
  // Triggers and right stick bind; small motion and the left stick do not.
  CHECK(!in.bind_pad(kZ, ax(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 5000)));
  CHECK(in.bind_pad(kZ, ax(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 30000))); CHECK(in.pad_label(kZ) == "RT");
  CHECK(!in.bind_pad(kCUp, ax(SDL_GAMEPAD_AXIS_LEFTY, -30000)));
  CHECK(in.bind_pad(kCUp, ax(SDL_GAMEPAD_AXIS_RIGHTY, -30000))); CHECK(in.pad_label(kCUp) == "Right stick up");
  // Keyboard swap.
  in.bind_key(0, kStickUp, SDL_SCANCODE_W); in.bind_key(0, kStickLeft, SDL_SCANCODE_A);
  CHECK(in.key(0, kStickLeft) == SDL_SCANCODE_A); CHECK(in.key(0, kL) == SDL_SCANCODE_LEFT);
  // Save, load into a fresh Input, compare.
  const auto file = std::filesystem::temp_directory_path() / "p64-input-tests.ini";
  Settings st; st.load(file); in.save_bindings(st); st.save();
  Settings st2; st2.load(file); Input in2; in2.init(); in2.load_bindings(st2);
  for (int b = 0; b < kButtonCount; ++b) CHECK(in2.pad_label(b) == in.pad_label(b));
  for (int c = 0; c < kControlCount; ++c) CHECK(in2.key(0, c) == in.key(0, c));
  // Reset.
  in2.reset_pad(); in2.reset_keys(0);
  CHECK(in2.pad_label(kA) == "A"); CHECK(in2.key(0, kStickUp) == SDL_SCANCODE_UP);
  // Corrupt settings fall back to defaults.
  Settings bad; bad.set("pad.a", "zz"); bad.set("keys.0.up", "99999"); Input in3; in3.init(); in3.load_bindings(bad);
  CHECK(in3.pad_label(kA) == "A"); CHECK(in3.key(0, kStickUp) == SDL_SCANCODE_UP);
  std::filesystem::remove(file);
  std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails != 0;
}
