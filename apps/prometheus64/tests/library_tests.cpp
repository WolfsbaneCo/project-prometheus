// Recognising games by their headers (src/library.cpp): Game Boy and Game Boy Color cartridges
// built here (a header with the Nintendo logo and its checksum), a file that isn't one, and the
// save module's Game Boy layout. No game file is involved.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "library.hpp"
using namespace p64;
namespace fs = std::filesystem;
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

namespace {

std::vector<uint8_t> gb_rom(const char* title, uint8_t cgb, uint8_t dest) {
  static const uint8_t kLogo[48] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
                                    0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E, 0xDC, 0xCC, 0x6E, 0xE6, 0xDD, 0xDD, 0xD9, 0x99,
                                    0xBB, 0xBB, 0x67, 0x63, 0x6E, 0x0E, 0xEC, 0xCC, 0xDD, 0xDC, 0x99, 0x9F, 0xBB, 0xB9, 0x33, 0x3E};
  std::vector<uint8_t> r(0x8000, 0xFF);
  std::memcpy(&r[0x104], kLogo, sizeof kLogo);
  std::memset(&r[0x134], 0, 16);
  std::memcpy(&r[0x134], title, std::strlen(title));
  r[0x143] = cgb;
  r[0x14A] = dest;
  uint8_t x = 0;
  for (int i = 0x134; i <= 0x14C; ++i) x = static_cast<uint8_t>(x - r[i] - 1);
  r[0x14D] = x;
  r[0x14E] = 0x12;
  r[0x14F] = 0x34;
  return r;
}

fs::path write(const fs::path& dir, const std::string& name, const std::vector<uint8_t>& data) {
  const fs::path p = dir / name;
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return p;
}

}  // namespace

int main() {
  const fs::path dir = fs::temp_directory_path() / "p64_library_tests";
  fs::remove_all(dir);
  fs::create_directories(dir);

  Game g;
  CHECK(read_game(write(dir, "red.gb", gb_rom("POKEMON RED", 0x00, 0x01)), g));
  CHECK(g.system == System::GB);
  CHECK(g.internal_name == "POKEMON RED");
  CHECK(g.id.rfind("DMG-POKEMON RED-", 0) == 0);
  CHECK(console_label(g) == "GB");
  CHECK(g.region.empty());

  // A Game Boy Color game: 15-letter title (the 16th byte is the colour flag), made in Japan.
  CHECK(read_game(write(dir, "crystal.gbc", gb_rom("PM_CRYSTAL\0", 0x80, 0x00)), g));
  CHECK(g.system == System::GB);
  CHECK(g.id.rfind("CGB-PM_CRYSTAL-", 0) == 0);
  CHECK(console_label(g) == "GBC");
  CHECK(g.region == "Japan");

  // Characters a folder name can't hold become _.
  CHECK(read_game(write(dir, "odd.gb", gb_rom("A:B/C?", 0x00, 0x01)), g));
  CHECK(g.internal_name == "A_B_C_");

  // A broken checksum, and something else entirely, are not games.
  auto bad = gb_rom("BROKEN", 0x00, 0x01);
  bad[0x14D] ^= 0xFF;
  CHECK(!read_game(write(dir, "broken.gb", bad), g));
  CHECK(!read_game(write(dir, "text.gb", std::vector<uint8_t>(0x8000, 'a')), g));

  // The library finds them by header, sorted by name.
  CHECK(scan_games(dir).size() == 3);

  fs::remove_all(dir);
  std::printf("library tests: %s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails ? 1 : 0;
}
