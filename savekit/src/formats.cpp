#include "savekit/formats.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace savekit {
namespace {

// A piece of a file that holds one memory.
struct Region {
  Memory memory;
  std::size_t offset;
  // SRAM and FlashRAM are kept by the emulators as host-order 32-bit words, which on every
  // little-endian PC reverses each group of four bytes relative to the N64.
  bool word_swapped;
};

// One file of a format.
struct Role {
  std::string key;
  std::size_t file_size;
  std::vector<Region> regions;
  // Project64 grows its files only as far as the game has written. Short files are normal
  // there and a warning sign anywhere else.
  bool short_is_normal;
};

std::vector<Role> roles_for(Format format) {
  const std::array<Memory, 4> paks = {Memory::ControllerPak1, Memory::ControllerPak2, Memory::ControllerPak3,
                                      Memory::ControllerPak4};
  std::vector<Role> roles;
  switch (format) {
    case Format::Canonical:
      for (Memory m : kAllMemories) {
        roles.push_back({std::string(memory_key(m)) + ".bin", memory_size(m), {{m, 0, false}}, false});
      }
      // The library also holds GBA saves this way (Memories/GBA/<game>/current/canonical/).
      roles.push_back({std::string(memory_key(Memory::GbaSave)) + ".bin", kGbaSaveSize, {{Memory::GbaSave, 0, false}}, false});
      break;
    case Format::Project64:
      roles.push_back({".eep", kEepromSize, {{Memory::Eeprom, 0, false}}, true});
      roles.push_back({".sra", kSramSize, {{Memory::Sram, 0, true}}, true});
      roles.push_back({".fla", kFlashRamSize, {{Memory::FlashRam, 0, true}}, true});
      for (std::size_t i = 0; i < paks.size(); ++i) {
        roles.push_back({"_Cont_" + std::to_string(i + 1) + ".mpk", kControllerPakSize, {{paks[i], 0, false}}, false});
      }
      break;
    case Format::Mupen64Plus: {
      roles.push_back({".eep", kEepromSize, {{Memory::Eeprom, 0, false}}, false});
      roles.push_back({".sra", kSramSize, {{Memory::Sram, 0, true}}, false});
      roles.push_back({".fla", kFlashRamSize, {{Memory::FlashRam, 0, true}}, false});
      Role mpk{".mpk", 4 * kControllerPakSize, {}, false};
      for (std::size_t i = 0; i < paks.size(); ++i) mpk.regions.push_back({paks[i], i * kControllerPakSize, false});
      roles.push_back(mpk);
      break;
    }
    case Format::RetroArchMupen64PlusNext: {
      // struct save_memory_data { eeprom[0x800]; mempack[4][0x8000]; sram[0x8000]; flashram[0x20000]; }
      Role srm{".srm", kEepromSize + 4 * kControllerPakSize + kSramSize + kFlashRamSize, {}, false};
      std::size_t at = 0;
      srm.regions.push_back({Memory::Eeprom, at, false});
      at += kEepromSize;
      for (Memory pak : paks) {
        srm.regions.push_back({pak, at, false});
        at += kControllerPakSize;
      }
      srm.regions.push_back({Memory::Sram, at, true});
      at += kSramSize;
      srm.regions.push_back({Memory::FlashRam, at, true});
      roles.push_back(srm);
      break;
    }
  }
  return roles;
}

constexpr std::array<FormatInfo, 4> kFormats = {{
    {Format::Canonical, "canonical", "Canonical master copy (N64 byte order, one file per memory)"},
    {Format::Project64, "project64", "Project64 (1.6 and newer)"},
    {Format::Mupen64Plus, "mupen64plus", "mupen64plus standalone (RMG, simple64)"},
    {Format::RetroArchMupen64PlusNext, "retroarch-mupen64plus-next", "RetroArch, Mupen64Plus-Next core (.srm)"},
}};

// Case-insensitive: Windows file names are, and emulators differ in capitalisation.
bool ends_with(std::string_view s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  return std::equal(suffix.begin(), suffix.end(), s.end() - static_cast<std::ptrdiff_t>(suffix.size()),
                    [](char a, char b) {
                      return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
                    });
}

}  // namespace

std::span<const FormatInfo> all_formats() { return kFormats; }

std::optional<Format> parse_format(std::string_view id) {
  for (const auto& f : kFormats) {
    if (f.id == id) return f.format;
  }
  return std::nullopt;
}

std::string_view format_id(Format format) {
  for (const auto& f : kFormats) {
    if (f.format == format) return f.id;
  }
  return "unknown";
}

std::vector<std::string> file_roles(Format format) {
  std::vector<std::string> keys;
  for (const auto& role : roles_for(format)) keys.push_back(role.key);
  return keys;
}

std::optional<std::string> match_role(Format format, std::string_view file_name) {
  // Longest suffix first so "_Cont_1.mpk" wins over a bare ".mpk".
  auto roles = roles_for(format);
  std::sort(roles.begin(), roles.end(), [](const Role& a, const Role& b) { return a.key.size() > b.key.size(); });
  for (const auto& role : roles) {
    if (format == Format::Canonical ? file_name == role.key : ends_with(file_name, role.key)) return role.key;
  }
  return std::nullopt;
}

Decoded decode(Format format, const FileSet& files) {
  const auto roles = roles_for(format);
  Decoded out;
  for (const auto& [key, data] : files) {
    auto role = std::find_if(roles.begin(), roles.end(), [&](const Role& r) { return r.key == key; });
    if (role == roles.end()) {
      throw FormatError("file role '" + key + "' is not part of format " + std::string(format_id(format)));
    }
    if (data.size() > role->file_size) {
      throw FormatError("'" + key + "' is " + std::to_string(data.size()) + " bytes; " +
                        std::string(format_id(format)) + " expects at most " + std::to_string(role->file_size));
    }
    if (data.size() < role->file_size) {
      std::string note = "'" + key + "' is " + std::to_string(data.size()) + " of " + std::to_string(role->file_size) +
                         " bytes; the rest is treated as never written";
      if (!role->short_is_normal) note += " (unexpected for this format)";
      out.notes.push_back(note);
    }
    out.shape[key] = data.size();

    for (const auto& region : role->regions) {
      if (region.offset >= data.size()) continue;
      const std::size_t size = memory_size(region.memory);
      // Pad in file layout, then convert. The fill is uniform, so padding is unaffected by the swap.
      Bytes mem(size, blank_fill(region.memory));
      const std::size_t available = std::min(size, data.size() - region.offset);
      std::memcpy(mem.data(), data.data() + region.offset, available);
      if (region.word_swapped) mem = swap_words(mem);
      if (is_uniform(mem, blank_fill(region.memory))) continue;  // Never written; nothing to carry.
      if (out.save.memories.count(region.memory)) {
        throw FormatError("two files both hold " + std::string(memory_key(region.memory)));
      }
      out.save.memories[region.memory] = std::move(mem);
    }
  }
  return out;
}

FileSet encode(Format format, const SaveSet& save, const Shape* shape) {
  for (const auto& [memory, data] : save.memories) {
    if (data.size() != memory_size(memory)) {
      throw FormatError(std::string(memory_key(memory)) + " is " + std::to_string(data.size()) +
                        " bytes; canonical size is " + std::to_string(memory_size(memory)));
    }
  }
  FileSet out;
  for (const auto& role : roles_for(format)) {
    std::size_t length = role.file_size;
    if (shape) {
      auto it = shape->find(role.key);
      if (it == shape->end()) continue;
      if (it->second > role.file_size) {
        throw FormatError("shape length for '" + role.key + "' exceeds the format's file size");
      }
      length = it->second;
    } else {
      bool any = std::any_of(role.regions.begin(), role.regions.end(),
                             [&](const Region& r) { return save.memories.count(r.memory) != 0; });
      if (!any) continue;
    }
    Bytes file(role.file_size, 0);
    for (const auto& region : role.regions) {
      auto it = save.memories.find(region.memory);
      Bytes mem = it != save.memories.end() ? it->second : Bytes(memory_size(region.memory), blank_fill(region.memory));
      if (region.word_swapped) mem = swap_words(mem);
      std::memcpy(file.data() + region.offset, mem.data(), mem.size());
    }
    file.resize(length);
    out[role.key] = std::move(file);
  }
  return out;
}

}  // namespace savekit
