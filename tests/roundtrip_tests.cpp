// Round-trip and golden-vector tests for every save format pair (brief sections 8.4 and 15).
// All data is synthetic: never real owner saves, never game files.

#include <cstring>
#include <string>

#include "check.hpp"
#include "savekit/formats.hpp"

using namespace savekit;

namespace {

// Deterministic pseudo-random bytes so failures reproduce.
Bytes noise(std::size_t size, std::uint32_t seed) {
  Bytes out(size);
  std::uint32_t x = seed * 2654435761u + 1;
  for (auto& b : out) {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    b = static_cast<std::uint8_t>(x);
  }
  return out;
}

// A memory with `used` bytes of data and the rest never written.
Bytes partly_used(Memory m, std::size_t used, std::uint32_t seed) {
  Bytes out(memory_size(m), blank_fill(m));
  Bytes data = noise(used, seed);
  std::memcpy(out.data(), data.data(), used);
  return out;
}

std::vector<std::pair<std::string, SaveSet>> sample_saves() {
  std::vector<std::pair<std::string, SaveSet>> s;
  s.push_back({"sram, game wrote 4 KB (Project64 1.6 style)", {{{Memory::Sram, partly_used(Memory::Sram, 4096, 1)}}}});
  s.push_back({"sram, full", {{{Memory::Sram, noise(kSramSize, 2)}}}});
  s.push_back({"eeprom 4 Kbit", {{{Memory::Eeprom, partly_used(Memory::Eeprom, kEeprom4kUsed, 3)}}}});
  s.push_back({"eeprom 16 Kbit", {{{Memory::Eeprom, noise(kEepromSize, 4)}}}});
  s.push_back({"flashram", {{{Memory::FlashRam, noise(kFlashRamSize, 5)}}}});
  s.push_back({"controller pak 1 and 3",
               {{{Memory::ControllerPak1, noise(kControllerPakSize, 6)},
                 {Memory::ControllerPak3, noise(kControllerPakSize, 7)}}}});
  SaveSet all;
  std::uint32_t seed = 10;
  for (Memory m : kAllMemories) all.memories[m] = noise(memory_size(m), seed++);
  s.push_back({"every memory", all});
  return s;
}

std::string label(Format a, Format b, const std::string& sample) {
  return std::string(format_id(a)) + " -> " + std::string(format_id(b)) + " -> back, " + sample;
}

}  // namespace

TEST_CASE("every format pair round-trips byte for byte") {
  for (const auto& [name, save] : sample_saves()) {
    for (const auto& a : all_formats()) {
      for (const auto& b : all_formats()) {
        const FileSet original = encode(a.format, save);
        const Decoded da = decode(a.format, original);
        const FileSet in_b = encode(b.format, da.save);
        const Decoded db = decode(b.format, in_b);
        const FileSet back = encode(a.format, db.save, &da.shape);
        if (back != original) std::cerr << "mismatch: " << label(a.format, b.format, name) << "\n";
        CHECK(back == original);
        CHECK(db.save == da.save);
      }
    }
  }
}

TEST_CASE("short Project64 files round-trip through every format") {
  // Project64 writes SRAM byte by byte at offset ^ 3 and never pads, so its files can stop
  // anywhere, including at a length that is not a multiple of 4.
  for (std::size_t length : {std::size_t{4096}, std::size_t{4093}, std::size_t{1}, std::size_t{kSramSize - 3}}) {
    FileSet pj = encode(Format::Project64, {{{Memory::Sram, noise(kSramSize, 42)}}});
    pj[".sra"].resize(length);
    const Decoded first = decode(Format::Project64, pj);
    CHECK(first.shape.at(".sra") == length);
    for (const auto& b : all_formats()) {
      const Decoded via = decode(b.format, encode(b.format, first.save));
      const FileSet back = encode(Format::Project64, via.save, &first.shape);
      if (back != pj) std::cerr << "mismatch: project64 (" << length << " bytes) via " << b.id << "\n";
      CHECK(back == pj);
    }
  }
}

TEST_CASE("golden: SRAM is word-swapped in Project64, mupen64plus and RetroArch files") {
  Bytes sram(kSramSize, 0xFF);
  std::memcpy(sram.data(), "EVERCART", 8);
  SaveSet s{{{Memory::Sram, sram}}};

  const auto pj = encode(Format::Project64, s);
  CHECK(std::memcmp(pj.at(".sra").data(), "REVETRAC", 8) == 0);
  CHECK(pj.at(".sra").size() == kSramSize);

  const auto m64p = encode(Format::Mupen64Plus, s);
  CHECK(m64p.at(".sra") == pj.at(".sra"));

  const auto srm = encode(Format::RetroArchMupen64PlusNext, s).at(".srm");
  CHECK(srm.size() == 0x48800);
  CHECK(std::memcmp(srm.data() + 0x20800, "REVETRAC", 8) == 0);

  const auto canon = encode(Format::Canonical, s);
  CHECK(std::memcmp(canon.at("sram.bin").data(), "EVERCART", 8) == 0);
}

TEST_CASE("golden: EEPROM and Controller Pak bytes are never swapped") {
  Bytes eep(kEepromSize, 0xFF);
  std::memcpy(eep.data(), "EVERCART", 8);
  Bytes pak2(kControllerPakSize, 0);
  std::memcpy(pak2.data(), "PAK2", 4);
  SaveSet s{{{Memory::Eeprom, eep}, {Memory::ControllerPak2, pak2}}};

  CHECK(encode(Format::Project64, s).at(".eep") == eep);
  CHECK(encode(Format::Project64, s).at("_Cont_2.mpk") == pak2);
  CHECK(encode(Format::Mupen64Plus, s).at(".eep") == eep);

  const auto mpk = encode(Format::Mupen64Plus, s).at(".mpk");
  CHECK(mpk.size() == 4 * kControllerPakSize);
  CHECK(std::memcmp(mpk.data() + kControllerPakSize, "PAK2", 4) == 0);

  const auto srm = encode(Format::RetroArchMupen64PlusNext, s).at(".srm");
  CHECK(std::memcmp(srm.data(), "EVERCART", 8) == 0);
  CHECK(std::memcmp(srm.data() + 0x800 + kControllerPakSize, "PAK2", 4) == 0);
}

TEST_CASE("golden: FlashRAM sits at the end of the RetroArch file, word-swapped") {
  Bytes fla(kFlashRamSize, 0xFF);
  std::memcpy(fla.data(), "EVERCART", 8);
  const auto srm = encode(Format::RetroArchMupen64PlusNext, {{{Memory::FlashRam, fla}}}).at(".srm");
  CHECK(std::memcmp(srm.data() + 0x28800, "REVETRAC", 8) == 0);
}

TEST_CASE("unused memories are not written as separate files") {
  const auto files = encode(Format::Mupen64Plus, {{{Memory::Sram, noise(kSramSize, 1)}}});
  CHECK(files.size() == 1);
  CHECK(files.count(".sra") == 1);
  // A memory that was never written decodes as absent.
  const auto d = decode(Format::RetroArchMupen64PlusNext, encode(Format::RetroArchMupen64PlusNext, {}));
  CHECK(d.save.memories.empty());
}

TEST_CASE("bad input is rejected, not guessed at") {
  CHECK_THROWS(decode(Format::Mupen64Plus, {{".sra", Bytes(kSramSize + 4, 0)}}));
  CHECK_THROWS(decode(Format::Mupen64Plus, {{".bogus", Bytes(4, 0)}}));
  CHECK_THROWS(encode(Format::Project64, {{{Memory::Sram, Bytes(100, 0)}}}));
  // Short files are fine for Project64 and flagged for formats that never write them short.
  CHECK(decode(Format::Project64, {{".sra", Bytes(4096, 1)}}).notes.size() == 1);
  const auto m64p = decode(Format::Mupen64Plus, {{".sra", Bytes(4096, 1)}});
  CHECK(m64p.notes.size() == 1 && m64p.notes[0].find("unexpected") != std::string::npos);
}

TEST_CASE("file names map to roles by suffix, ignoring case") {
  CHECK(match_role(Format::Project64, "HARVESTMOON64.sra") == std::optional<std::string>(".sra"));
  CHECK(match_role(Format::Project64, "GAME.SRA") == std::optional<std::string>(".sra"));
  CHECK(match_role(Format::Project64, "GAME_Cont_3.mpk") == std::optional<std::string>("_Cont_3.mpk"));
  CHECK(!match_role(Format::Project64, "GAME.mpk").has_value());
  CHECK(match_role(Format::Mupen64Plus, "Some Game (U).mpk") == std::optional<std::string>(".mpk"));
  CHECK(match_role(Format::Canonical, "sram.bin") == std::optional<std::string>("sram.bin"));
  CHECK(!match_role(Format::Canonical, "xsram.bin").has_value());
}

int main() { return check::run_all(); }
