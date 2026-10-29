// A GBA game without a window, as fast as the emulator runs, driven by a script: the test bench for
// the event gift station (src/gift_station.cpp), and for anything else that needs a game played to
// a point. Not built by default:
//   cmake --build build --target p64_giftsim
//   p64_giftsim <core library> <script file>
//
// Script, one command per line (# starts a comment):
//   rom <file>                 the game (loaded by `boot`)
//   savefile <file>            the cartridge save to start from (missing file: a blank cartridge)
//   option <key>=<value>       a core option, before `boot`
//   boot                       load the game
//   run <frames>               play with nothing pressed
//   press <buttons> [frames]   hold buttons (A+UP) for 6 frames or the number given, then 10 idle
//   hold <buttons> <frames>    hold without the idle frames after
//   shot <file.ppm>            the current picture
//   state save|load <file>     a save state
//   writesave <file>           the cartridge save as it is now
//   dumpmem <addr> <len> <file> memory as the console sees it (hex address)
//   peek8|peek16|peek32 <addr> print memory (hex address)
//   poke8|poke16|poke32 <addr> <value>
//   flag <id>                  print an Emerald event flag (hex id), from the save block in RAM
//   station <gift> [code]      open the event gift station with this gift (game code: BPEE)
//   until <text> <frames>      run until the station's log contains the text (or give up)
#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core.hpp"
#include "gift_station.hpp"

namespace {

using p64::Core;

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool write_file(const std::string& path, const uint8_t* data, std::size_t size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
  return static_cast<bool>(out);
}

uint16_t parse_buttons(const std::string& s) {
  static const struct {
    const char* name;
    unsigned id;
  } kNames[] = {{"A", RETRO_DEVICE_ID_JOYPAD_A},         {"B", RETRO_DEVICE_ID_JOYPAD_B},
                {"SELECT", RETRO_DEVICE_ID_JOYPAD_SELECT}, {"START", RETRO_DEVICE_ID_JOYPAD_START},
                {"UP", RETRO_DEVICE_ID_JOYPAD_UP},       {"DOWN", RETRO_DEVICE_ID_JOYPAD_DOWN},
                {"LEFT", RETRO_DEVICE_ID_JOYPAD_LEFT},   {"RIGHT", RETRO_DEVICE_ID_JOYPAD_RIGHT},
                {"L", RETRO_DEVICE_ID_JOYPAD_L},         {"R", RETRO_DEVICE_ID_JOYPAD_R}};
  uint16_t mask = 0;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, '+')) {
    bool found = false;
    for (const auto& n : kNames) {
      if (part == n.name) {
        mask |= static_cast<uint16_t>(1u << n.id);
        found = true;
      }
    }
    if (!found) std::cerr << "unknown button " << part << "\n";
  }
  return mask;
}

struct Bench {
  Core core;
  std::vector<uint16_t> frame;  // RGB565
  unsigned w = 0, h = 0;
  uint16_t buttons = 0;
  uint64_t frames = 0;
  p64::GiftStation station;

  void step(int n) {
    for (int i = 0; i < n; ++i) {
      core.run();
      station.update();
      ++frames;
    }
  }

  void shot(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (uint16_t px : frame) {
      const uint8_t r = static_cast<uint8_t>(((px >> 11) & 31) * 255 / 31);
      const uint8_t g = static_cast<uint8_t>(((px >> 5) & 63) * 255 / 63);
      const uint8_t b = static_cast<uint8_t>((px & 31) * 255 / 31);
      out.put(static_cast<char>(r)).put(static_cast<char>(g)).put(static_cast<char>(b));
    }
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: p64_giftsim <core library> <script>\n";
    return 2;
  }
  Bench b;
  b.core.system_dir = std::filesystem::temp_directory_path() / "p64_giftsim";
  b.core.save_dir = b.core.system_dir;
  std::filesystem::create_directories(b.core.system_dir);
  b.core.hooks.video = [&](const void* data, unsigned width, unsigned height, std::size_t pitch) {
    if (!data) return;
    b.w = width;
    b.h = height;
    b.frame.resize(static_cast<std::size_t>(width) * height);
    for (unsigned y = 0; y < height; ++y)
      std::memcpy(&b.frame[static_cast<std::size_t>(y) * width], static_cast<const uint8_t*>(data) + y * pitch, width * 2);
  };
  b.core.hooks.audio = [](const int16_t*, std::size_t) {};
  b.core.hooks.input_poll = [] {};
  b.core.hooks.input_state = [&](unsigned port, unsigned device, unsigned, unsigned id) -> int16_t {
    if (port != 0 || device != RETRO_DEVICE_JOYPAD) return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(b.buttons);
    return (b.buttons >> id) & 1;
  };
  b.core.hooks.message = [](const std::string& m) { std::cerr << "[core] " << m << "\n"; };
  if (std::string err = b.core.open(argv[1]); !err.empty()) {
    std::cerr << "core: " << err << "\n";
    return 1;
  }

  std::string rom, savefile;
  std::ifstream script(argv[2]);
  std::string line;
  int lineno = 0;
  while (std::getline(script, line)) {
    ++lineno;
    if (const auto hash = line.find('#'); hash != std::string::npos) line.resize(hash);
    std::stringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd)) continue;
    auto fail = [&](const std::string& why) {
      std::cerr << "line " << lineno << ": " << why << "\n";
      std::exit(1);
    };
    if (cmd == "rom") {
      ss >> rom;
    } else if (cmd == "savefile") {
      ss >> savefile;
    } else if (cmd == "option") {
      std::string kv;
      ss >> kv;
      const auto eq = kv.find('=');
      b.core.set_option(kv.substr(0, eq), kv.substr(eq + 1));
    } else if (cmd == "boot") {
      if (std::string err = b.core.load_game(rom, read_file(rom), true); !err.empty()) fail(err);
      if (!savefile.empty()) {
        const std::vector<uint8_t> save = read_file(savefile);
        if (uint8_t* ram = b.core.save_ram(); ram && !save.empty())
          std::memcpy(ram, save.data(), std::min(save.size(), b.core.save_ram_size()));
      }
      if (const auto* np = b.core.netpacket()) b.station.attach(np);
    } else if (cmd == "run") {
      int n = 0;
      ss >> n;
      b.buttons = 0;
      b.step(n);
    } else if (cmd == "press" || cmd == "hold") {
      std::string names;
      int n = cmd == "press" ? 6 : 1;
      ss >> names >> n;
      if (n <= 0) n = 6;
      b.buttons = parse_buttons(names);
      b.step(n);
      b.buttons = 0;
      if (cmd == "press") b.step(10);
    } else if (cmd == "shot") {
      std::string path;
      ss >> path;
      b.shot(path);
    } else if (cmd == "state") {
      std::string what, path;
      ss >> what >> path;
      if (what == "save") {
        const std::vector<uint8_t> st = b.core.save_state();
        write_file(path, st.data(), st.size());
      } else if (!b.core.load_state(read_file(path))) {
        fail("could not load state " + path);
      }
    } else if (cmd == "dumpmem") {
      std::string a, path;
      std::size_t len = 0;
      ss >> a >> len >> path;
      const uint8_t* p = b.core.memory(static_cast<uint32_t>(std::stoul(a, nullptr, 16)), len);
      if (!p) fail("address not mapped: " + a);
      write_file(path, p, len);
    } else if (cmd == "writesave") {
      std::string path;
      ss >> path;
      write_file(path, b.core.save_ram(), b.core.save_ram_size());
    } else if (cmd.rfind("peek", 0) == 0 || cmd.rfind("poke", 0) == 0) {
      const int bytes = std::atoi(cmd.c_str() + 4) / 8;
      std::string a, v;
      ss >> a >> v;
      const uint32_t addr = static_cast<uint32_t>(std::stoul(a, nullptr, 16));
      uint8_t* p = b.core.memory(addr, static_cast<std::size_t>(bytes));
      if (!p) fail("address not mapped: " + a);
      if (cmd[1] == 'e') {
        uint32_t value = 0;
        std::memcpy(&value, p, static_cast<std::size_t>(bytes));
        std::printf("%08x: %0*x\n", addr, bytes * 2, value);
      } else {
        const uint32_t value = static_cast<uint32_t>(std::stoul(v, nullptr, 0));
        std::memcpy(p, &value, static_cast<std::size_t>(bytes));
      }
    } else if (cmd == "flag") {
      std::string a;
      ss >> a;
      const uint32_t id = static_cast<uint32_t>(std::stoul(a, nullptr, 16));
      uint32_t sb1 = 0;
      if (uint8_t* p = b.core.memory(0x03005D8C, 4)) std::memcpy(&sb1, p, 4);
      uint8_t* f = b.core.memory(sb1 + 0x1270 + id / 8, 1);
      std::printf("flag %x: %s\n", id, f ? ((*f >> (id % 8)) & 1 ? "set" : "clear") : "?");
    } else if (cmd == "station") {
      std::string key, code = "BPEE";
      ss >> key >> code;
      bool found = false;
      for (const p64::EventGift& g : p64::event_gifts(code)) {
        if (g.key == key) {
          b.station.open(g);
          found = true;
        }
      }
      if (!found) fail("no such gift: " + key);
    } else if (cmd == "until") {
      std::string text;
      int n = 0;
      ss >> text >> n;
      b.buttons = 0;
      int i = 0;
      while (i < n && b.station.log().find(text) == std::string::npos) {
        b.step(1);
        ++i;
      }
      std::printf("until %s: %s after %d frames\n", text.c_str(), i < n ? "reached" : "NOT reached", i);
      std::fflush(stdout);
    } else {
      fail("unknown command " + cmd);
    }
  }
  b.station.close();
  b.core.close();
  return 0;
}
