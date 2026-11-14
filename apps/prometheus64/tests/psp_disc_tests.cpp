// PSP disc images (src/psp_disc.cpp): a tiny UMD image built here (an ISO 9660 file system with
// PSP_GAME/PARAM.SFO), the same compressed as a .cso, and an EBOOT.PBP, read back for the game's
// ID and title. No game file is involved.
#include <zlib.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "psp_disc.hpp"
using namespace p64;
namespace fs = std::filesystem;
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

namespace {

using Bytes = std::vector<uint8_t>;
constexpr std::size_t kSector = 2048;

void put16(Bytes& b, std::size_t at, uint16_t v) { std::memcpy(&b[at], &v, 2); }
void put32(Bytes& b, std::size_t at, uint32_t v) { std::memcpy(&b[at], &v, 4); }

// PARAM.SFO with the two entries the app reads.
Bytes sfo(const std::string& id, const std::string& title) {
  const std::string keys = std::string("DISC_ID") + '\0' + "TITLE" + '\0';
  const std::size_t key_table = 20 + 2 * 16, data_table = (key_table + keys.size() + 3) / 4 * 4;
  Bytes b(data_table + 16 + 128, 0);
  std::memcpy(&b[0], "\0PSF", 4);
  put32(b, 4, 0x101);
  put32(b, 8, static_cast<uint32_t>(key_table));
  put32(b, 12, static_cast<uint32_t>(data_table));
  put32(b, 16, 2);
  auto entry = [&](std::size_t i, uint16_t key_at, uint32_t len, uint32_t max, uint32_t data_at) {
    const std::size_t e = 20 + i * 16;
    put16(b, e, key_at);
    put16(b, e + 2, 0x0204);  // a string
    put32(b, e + 4, len);
    put32(b, e + 8, max);
    put32(b, e + 12, data_at);
  };
  entry(0, 0, static_cast<uint32_t>(id.size() + 1), 16, 0);
  entry(1, 8, static_cast<uint32_t>(title.size() + 1), 128, 16);
  std::memcpy(&b[key_table], keys.data(), keys.size());
  std::memcpy(&b[data_table], id.data(), id.size());
  std::memcpy(&b[data_table + 16], title.data(), title.size());
  return b;
}

// One directory record.
void record(Bytes& img, std::size_t at, const std::string& name, uint32_t extent, uint32_t size, bool dir) {
  const std::size_t len = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
  img[at] = static_cast<uint8_t>(len);
  put32(img, at + 2, extent);
  put32(img, at + 10, size);
  img[at + 25] = dir ? 2 : 0;
  img[at + 32] = static_cast<uint8_t>(name.size());
  std::memcpy(&img[at + 33], name.data(), name.size());
}

// Sector 16: the volume descriptor; 20: the root; 21: PSP_GAME; 22: PARAM.SFO.
Bytes iso(const Bytes& param) {
  Bytes img(24 * kSector, 0);
  img[16 * kSector] = 1;
  std::memcpy(&img[16 * kSector + 1], "CD001", 5);
  record(img, 16 * kSector + 156, std::string(1, '\0'), 20, kSector, true);
  record(img, 20 * kSector, std::string(1, '\0'), 20, kSector, true);
  record(img, 20 * kSector + 34, std::string(1, '\1'), 20, kSector, true);
  record(img, 20 * kSector + 68, "PSP_GAME", 21, kSector, true);
  record(img, 21 * kSector, std::string(1, '\0'), 21, kSector, true);
  record(img, 21 * kSector + 34, std::string(1, '\1'), 20, kSector, true);
  record(img, 21 * kSector + 68, "PARAM.SFO;1", 22, static_cast<uint32_t>(param.size()), false);
  std::memcpy(&img[22 * kSector], param.data(), param.size());
  return img;
}

// The same image as a CISO: every sector deflated, except one stored plain (as real ones have).
Bytes cso(const Bytes& img) {
  const uint32_t blocks = static_cast<uint32_t>(img.size() / kSector);
  Bytes out(24 + (blocks + 1) * 4, 0);
  std::memcpy(&out[0], "CISO", 4);
  put32(out, 4, 24);
  put32(out, 8, static_cast<uint32_t>(img.size()));
  put32(out, 16, kSector);
  out[20] = 1;
  for (uint32_t b = 0; b < blocks; ++b) {
    const bool plain = b == 21;
    put32(out, 24 + b * 4, static_cast<uint32_t>(out.size()) | (plain ? 0x80000000u : 0));
    if (plain) {
      out.insert(out.end(), img.begin() + b * kSector, img.begin() + (b + 1) * kSector);
      continue;
    }
    Bytes packed(kSector * 2);
    z_stream z{};
    deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    z.next_in = const_cast<Bytef*>(&img[b * kSector]);
    z.avail_in = kSector;
    z.next_out = packed.data();
    z.avail_out = static_cast<uInt>(packed.size());
    deflate(&z, Z_FINISH);
    packed.resize(z.total_out);
    deflateEnd(&z);
    out.insert(out.end(), packed.begin(), packed.end());
  }
  put32(out, 24 + blocks * 4, static_cast<uint32_t>(out.size()));
  return out;
}

Bytes pbp(const Bytes& param) {
  Bytes out(40, 0);
  std::memcpy(&out[0], "\0PBP", 4);
  put32(out, 4, 0x10000);
  put32(out, 8, 40);
  for (int i = 1; i < 8; ++i) put32(out, 8 + static_cast<std::size_t>(i) * 4, static_cast<uint32_t>(40 + param.size()));
  out.insert(out.end(), param.begin(), param.end());
  return out;
}

// A CHD (version 5) whose only content that matters here is its metadata list: one entry tagged
// `tag` ("DVD " for a UMD made with chdman createdvd, "CHT2" for a CD).
Bytes chd(const char* tag) {
  Bytes b(200, 0);
  std::memcpy(&b[0], "MComprHD", 8);
  b[11] = 124;  // header length
  b[15] = 5;    // version
  b[55] = 124;  // metadata at 124
  std::memcpy(&b[124], tag, 4);
  return b;
}

fs::path write(const fs::path& dir, const char* name, const Bytes& data) {
  const fs::path file = dir / name;
  std::ofstream(file, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return file;
}

}  // namespace

int main() {
  const fs::path dir = fs::temp_directory_path() / "p64_psp_disc_tests";
  fs::create_directories(dir);
  const Bytes param = sfo("ULUS-10041", "A Test Game");
  const Bytes image = iso(param);
  psp::DiscInfo d;
  // A plain image.
  CHECK(psp::read_disc(write(dir, "game.iso", image), d));
  CHECK(d.disc_id == "ULUS10041" && d.title == "A Test Game" && d.region == "USA");
  // The same, compressed.
  d = {};
  CHECK(psp::read_disc(write(dir, "game.cso", cso(image)), d));
  CHECK(d.disc_id == "ULUS10041" && d.title == "A Test Game");
  // A PSN game or homebrew: its EBOOT.
  d = {};
  CHECK(psp::read_disc(write(dir, "EBOOT.PBP", pbp(sfo("NPEH00020", "Another"))), d));
  CHECK(d.disc_id == "NPEH00020" && d.title == "Another" && d.region == "Europe");
  // Not PSP games: another console's disc image (no PSP_GAME), a file of nothing, a cut-off image.
  Bytes other = image;
  std::memcpy(&other[20 * kSector + 68 + 33], "PS2_GAME", 8);
  CHECK(!psp::read_disc(write(dir, "other.iso", other), d));
  CHECK(!psp::read_disc(write(dir, "empty.iso", Bytes(64 * 1024, 0)), d));
  CHECK(!psp::read_disc(write(dir, "short.iso", Bytes(image.begin(), image.begin() + 17 * kSector)), d));
  CHECK(!psp::read_disc(dir / "missing.iso", d));
  // CHDs: a DVD-type one (a UMD) is taken, named by its file; a CD (PlayStation, Saturn) isn't.
  d = {};
  CHECK(psp::read_disc(write(dir, "umd.chd", chd("DVD ")), d) && d.disc_id.empty());
  CHECK(!psp::read_disc(write(dir, "ps1.chd", chd("CHT2")), d));
  // Regions, from the ID's third letter.
  CHECK(psp::region_of("ULJM05800") == "Japan" && psp::region_of("UCES00001") == "Europe" && psp::region_of("X") == "");
  CHECK(psp::is_psp_extension("a/b/Game.ISO") && psp::is_psp_extension("x.cso") && !psp::is_psp_extension("x.gba"));
  std::error_code ec;
  fs::remove_all(dir, ec);
  std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails != 0;
}
