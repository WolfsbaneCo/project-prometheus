// Prometheus 64: an N64 player built around the Mupen64Plus-Next emulator core.
//
//   Prometheus64 [game file]
//
// Test options (used by automated checks): --core <path> --size WxH --frames N --screenshot <png>
// --press <frame>:<key name> --hold <from>-<to>:<player>:<button> --poke-save <frame>
// --dump-save <file> --fullscreen

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <string>

#include "app.hpp"

int main(int argc, char** argv) {
  p64::Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--core") {
      o.core = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(next().c_str())));
    } else if (a == "--screenshot") {
      o.screenshot = next();
    } else if (a == "--frames") {
      o.frames = std::atoi(next().c_str());
    } else if (a == "--size") {
      const std::string v = next();
      o.width = std::atoi(v.c_str());
      o.height = std::atoi(v.substr(v.find('x') + 1).c_str());
    } else if (a == "--fullscreen") {
      o.fullscreen = true;
    } else if (a == "--press") {
      o.keys.push_back(next());
    } else if (a == "--poke-save") {
      o.poke_frame = std::atoi(next().c_str());
    } else if (a == "--dump-save") {
      o.dump_save = next();
    } else if (a == "--click") {
      o.clicks.push_back(next());
    } else if (a == "--hold") {
      o.holds.push_back(next());
    } else if (!a.empty() && a[0] != '-') {
      o.rom = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(a.c_str())));
    }
  }
  return p64::App(std::move(o)).run();
}
