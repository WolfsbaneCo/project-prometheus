#include "savekit/inspect.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "savekit/bytes.hpp"
#include "savekit/formats.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;

namespace savekit {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string hex_offset(std::size_t v) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "0x%zX", v);
  return buf;
}

std::string kb(std::size_t bytes) {
  char buf[32];
  if (bytes % 1024 == 0) {
    std::snprintf(buf, sizeof buf, "%zu KB", bytes / 1024);
  } else {
    std::snprintf(buf, sizeof buf, "%zu bytes", bytes);
  }
  return buf;
}

// The longest run of printable ASCII, as a hint to the byte order a human can check by eye.
std::string longest_text(const Bytes& data) {
  std::size_t best_at = 0, best_len = 0, run_at = 0, run_len = 0;
  for (std::size_t i = 0; i < data.size(); ++i) {
    if (data[i] >= 0x20 && data[i] < 0x7F) {
      if (run_len == 0) run_at = i;
      if (++run_len > best_len) {
        best_len = run_len;
        best_at = run_at;
      }
    } else {
      run_len = 0;
    }
  }
  return std::string(reinterpret_cast<const char*>(data.data()) + best_at, best_len);
}

// Rough "looks like English or a romanised name" score from common letter pairs.
int text_score(const std::string& s) {
  static const std::array<std::string_view, 40> pairs = {
      "TH", "HE", "IN", "ER", "AN", "RE", "ND", "AT", "ON", "NT", "HA", "ES", "ST", "EN",
      "ED", "TO", "IT", "OU", "EA", "HI", "IS", "OR", "TI", "AS", "TE", "ET", "NG", "OF",
      "AL", "DE", "SE", "LE", "SA", "SI", "AR", "VE", "RA", "LD", "UR", "MO"};
  std::string u;
  for (char c : s) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  int score = 0;
  for (std::size_t i = 0; i + 1 < u.size(); ++i) {
    for (auto p : pairs) {
      if (u[i] == p[0] && u[i + 1] == p[1]) ++score;
    }
  }
  return score;
}

void describe_used_area(const Bytes& data, std::vector<std::string>& facts) {
  std::size_t blocks = 0, total = (data.size() + 511) / 512;
  for (std::size_t b = 0; b < total; ++b) {
    auto begin = data.begin() + static_cast<std::ptrdiff_t>(b * 512);
    auto end = data.begin() + static_cast<std::ptrdiff_t>(std::min(data.size(), (b + 1) * 512));
    if (std::any_of(begin, end, [](std::uint8_t v) { return v != 0x00 && v != 0xFF; })) ++blocks;
  }
  facts.push_back("512-byte blocks holding data: " + std::to_string(blocks) + " of " + std::to_string(total));
}

void describe_word_order(const Bytes& data, std::vector<std::string>& facts) {
  if (data.size() % 4 != 0) {
    facts.push_back("length is not a multiple of 4; byte order cannot be tested");
    return;
  }
  const std::string as_is = longest_text(data);
  const std::string swapped = longest_text(swap_words(data));
  if (as_is.size() < 6) {
    facts.push_back("no readable text found; byte order cannot be confirmed from content");
    return;
  }
  facts.push_back("longest text, bytes as stored: \"" + as_is + "\"");
  facts.push_back("longest text, each 4-byte word reversed: \"" + swapped + "\"");
  const int a = text_score(as_is), b = text_score(swapped);
  if (b > a) {
    facts.push_back("byte order: word-swapped (host order), as Project64 and mupen64plus write it on a PC");
  } else if (a > b) {
    facts.push_back("byte order: N64 order (big-endian), as a cartridge dump would be");
  } else {
    facts.push_back("byte order: undetermined from text; compare the two strings above");
  }
}

void describe_controller_pak(const Bytes& pak, const std::string& label, std::vector<std::string>& facts) {
  // The note table lives in pages 3 and 4: 16 entries of 32 bytes. An entry is in use when it
  // names a game and points at a data page past the 5 reserved ones.
  int notes = 0;
  for (int i = 0; i < 16; ++i) {
    std::size_t at = 0x300 + static_cast<std::size_t>(i) * 32;
    if (at + 32 > pak.size()) break;
    bool has_code = pak[at] || pak[at + 1] || pak[at + 2] || pak[at + 3];
    unsigned start = (unsigned{pak[at + 6]} << 8) | pak[at + 7];
    if (has_code && start >= 5 && start < 128) ++notes;
  }
  facts.push_back(label + ": " + std::to_string(notes) + " note(s) in the note table");
}

void inspect_save(const fs::path& path, const Bytes& data, FileReport& r) {
  const std::string ext = lower(path.extension().string());
  auto& facts = r.facts;

  if (ext == ".sra") {
    r.kind = "in-game save: SRAM";
    facts.push_back("SRAM chip size is " + kb(kSramSize) + "; file is " + kb(data.size()));
    if (data.size() < kSramSize) {
      facts.push_back(
          "shorter than the chip: Project64 grows this file only as far as the game has written. "
          "Copying it as-is into mupen64plus or RetroArch is unsafe (mupen64plus leaves the rest of "
          "SRAM uninitialised); convert it, which pads to 32 KB");
    } else if (data.size() > kSramSize) {
      facts.push_back("larger than 32 KB: not a standard SRAM save (96 KB Dezaemon 3D layout is not supported)");
    }
    describe_used_area(data, facts);
    describe_word_order(data, facts);
  } else if (ext == ".fla") {
    r.kind = "in-game save: FlashRAM";
    facts.push_back("FlashRAM size is " + kb(kFlashRamSize) + "; file is " + kb(data.size()));
    describe_used_area(data, facts);
    describe_word_order(data, facts);
  } else if (ext == ".eep") {
    r.kind = "in-game save: EEPROM";
    facts.push_back("file is " + kb(data.size()) + "; 4 Kbit chips use 512 bytes, 16 Kbit chips 2 KB");
    std::size_t last = 0;
    for (std::size_t i = 0; i < data.size(); ++i) {
      if (data[i] != 0xFF) last = i + 1;
    }
    facts.push_back("data ends at " + hex_offset(last) +
                    (last <= kEeprom4kUsed ? " (fits a 4 Kbit chip)" : " (needs a 16 Kbit chip)"));
  } else if (ext == ".mpk") {
    r.kind = "in-game save: Controller Pak";
    if (data.size() == kControllerPakSize) {
      describe_controller_pak(data, "pak", facts);
    } else if (data.size() == 4 * kControllerPakSize) {
      facts.push_back("four paks in one file (mupen64plus layout)");
      for (std::size_t i = 0; i < 4; ++i) {
        Bytes pak(data.begin() + static_cast<std::ptrdiff_t>(i * kControllerPakSize),
                  data.begin() + static_cast<std::ptrdiff_t>((i + 1) * kControllerPakSize));
        describe_controller_pak(pak, "pak " + std::to_string(i + 1), facts);
      }
    } else {
      facts.push_back("unexpected size " + kb(data.size()) + " for a Controller Pak");
    }
  } else if (ext == ".srm") {
    r.kind = "in-game save: RetroArch combined file";
    try {
      auto decoded = decode(Format::RetroArchMupen64PlusNext, {{".srm", data}});
      std::string held;
      for (const auto& [memory, bytes] : decoded.save.memories) {
        held += (held.empty() ? "" : ", ") + std::string(memory_key(memory));
      }
      facts.push_back("Mupen64Plus-Next layout; memories holding data: " + (held.empty() ? "none" : held));
      for (const auto& n : decoded.notes) facts.push_back(n);
    } catch (const FormatError& e) {
      facts.push_back(std::string("not a Mupen64Plus-Next .srm: ") + e.what());
    }
  }
}

std::uint16_t le16(const Bytes& b, std::size_t at) {
  return static_cast<std::uint16_t>(b[at] | (b[at + 1] << 8));
}
std::uint32_t le32(const Bytes& b, std::size_t at) {
  return std::uint32_t{b[at]} | (std::uint32_t{b[at + 1]} << 8) | (std::uint32_t{b[at + 2]} << 16) |
         (std::uint32_t{b[at + 3]} << 24);
}

void describe_state_payload(std::uint32_t size, std::vector<std::string>& facts) {
  if (size >= 0x400000 && size < 0x410000) {
    facts.push_back("size is consistent with 4 MB of console RAM (no Expansion Pak)");
  } else if (size >= 0x800000 && size < 0x810000) {
    facts.push_back("size is consistent with 8 MB of console RAM (Expansion Pak on)");
  }
}

void inspect_state(const Bytes& data, bool zipped, FileReport& r) {
  r.kind = zipped ? "save state: Project64 (zipped)" : "save state: Project64";
  auto& facts = r.facts;
  if (!zipped) {
    if (data.size() >= 8 && le32(data, 0) == 0x23D8A6C8) {
      facts.push_back("Project64 state header found; RAM size " + std::to_string(le32(data, 4)) + " bytes");
    }
  } else {
    // Read the ZIP central directory only; nothing is decompressed.
    std::size_t eocd = std::string::npos;
    if (data.size() >= 22) {
      const std::size_t lowest = data.size() > 0xFFFF + 22 ? data.size() - 0xFFFF - 22 : 0;
      for (std::size_t i = data.size() - 22 + 1; i-- > lowest;) {
        if (le32(data, i) == 0x06054b50) {
          eocd = i;
          break;
        }
      }
    }
    if (eocd == std::string::npos) {
      facts.push_back("not a readable ZIP archive");
    } else {
      std::size_t entries = le16(data, eocd + 10);
      std::size_t at = le32(data, eocd + 16);
      for (std::size_t e = 0; e < entries && at + 46 <= data.size() && le32(data, at) == 0x02014b50; ++e) {
        std::uint16_t time = le16(data, at + 12), date = le16(data, at + 14);
        std::uint32_t usize = le32(data, at + 24);
        std::size_t nlen = le16(data, at + 28), elen = le16(data, at + 30), clen = le16(data, at + 32);
        if (at + 46 + nlen > data.size()) break;
        std::string inner(reinterpret_cast<const char*>(&data[at + 46]), nlen);
        char when[48] = "with no date";
        if (date != 0) {
          std::snprintf(when, sizeof when, "stamped %04d-%02d-%02d %02d:%02d", 1980 + (date >> 9), (date >> 5) & 15,
                        date & 31, time >> 11, (time >> 5) & 63);
        }
        facts.push_back("contains \"" + inner + "\", " + std::to_string(usize) + " bytes, " + when);
        describe_state_payload(usize, facts);
        at += 46 + nlen + elen + clen;
      }
    }
  }
  facts.push_back(
      "a save state is a snapshot of one emulator version; it will not load in this app. "
      "Only in-game saves are carried across");
}

bool is_state_name(const std::string& lower_name, bool& zipped) {
  std::string n = lower_name;
  zipped = n.size() > 4 && n.compare(n.size() - 4, 4, ".zip") == 0;
  if (zipped) n.resize(n.size() - 4);
  auto dot = n.rfind('.');
  if (dot == std::string::npos) return false;
  std::string ext = n.substr(dot + 1);
  return ext == "pj" || (ext.size() == 3 && ext[0] == 'p' && ext[1] == 'j' && std::isdigit(static_cast<unsigned char>(ext[2])));
}

bool is_save_ext(const std::string& ext) {
  return ext == ".sra" || ext == ".eep" || ext == ".fla" || ext == ".mpk" || ext == ".srm";
}

void inspect_file(const fs::path& path, std::vector<FileReport>& out) {
  const std::string lname = lower(path.filename().string());
  const std::string ext = lower(path.extension().string());
  bool zipped = false;
  const bool state = is_state_name(lname, zipped);
  if (!state && !is_save_ext(ext)) return;

  FileReport r;
  r.path = path;
  const Bytes data = read_file(path);
  r.size = data.size();
  r.sha256 = sha256_hex(data);
  if (state) {
    inspect_state(data, zipped, r);
  } else {
    inspect_save(path, data, r);
  }
  out.push_back(std::move(r));
}

}  // namespace

std::vector<FileReport> inspect_path(const fs::path& path) {
  std::vector<FileReport> out;
  if (fs::is_directory(path)) {
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(path)) {
      if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) inspect_file(f, out);
  } else {
    inspect_file(path, out);
  }
  return out;
}

}  // namespace savekit
