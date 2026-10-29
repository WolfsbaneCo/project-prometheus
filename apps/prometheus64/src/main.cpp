// Project Prometheus: a player for classic consoles (N64, GBA), built around libretro emulator cores.
//
//   Prometheus64 [game file]
//
// Test options (used by automated checks): --core <path> --size WxH --frames N --screenshot <png>
// --press <frame>:<key name> --hold <from>-<to>:<player>:<button> --poke-save <frame>
// --dump-save <file> --fullscreen --ram-hash --option key=value --click <frame>:x,y
// Online tests: --online-host (with a game) --online-join <code> --online-start --online-exit-frame N
// --online-setup <key> (against P64_CLOUDFLARE_API) --save-key <file>

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
      const std::string v = next();  // "<frame>" or "<frame>:<8 characters>"
      o.poke_frame = std::atoi(v.c_str());
      if (const auto colon = v.find(':'); colon != std::string::npos) o.poke_text = v.substr(colon + 1, 8);
    } else if (a == "--dump-save") {
      o.dump_save = next();
    } else if (a == "--ram-hash") {
      o.ram_hash = true;
    } else if (a == "--option") {
      o.core_options.push_back(next());
    } else if (a == "--click") {
      o.clicks.push_back(next());
    } else if (a == "--online-host") {
      o.online_host = true;
    } else if (a == "--online-join") {
      o.online_join = next();
    } else if (a == "--backup-connect") {
      o.backup_connect = true;
    } else if (a == "--backup-sync") {
      o.backup_sync = true;
    } else if (a == "--save-key") {
      o.save_key = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(next().c_str())));
    } else if (a == "--online-setup") {
      o.setup_token = next();
    } else if (a == "--online-start") {
      o.online_start = true;
    } else if (a == "--online-exit-frame") {
      o.online_exit_frame = static_cast<uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
    } else if (a == "--hold") {
      o.holds.push_back(next());
    } else if (!a.empty() && a[0] != '-') {
      o.rom = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(a.c_str())));
    }
  }
  return p64::App(std::move(o)).run();
}
