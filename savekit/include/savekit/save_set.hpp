#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>

#include "savekit/bytes.hpp"

namespace savekit {

// One kind of cartridge or accessory memory that holds an in-game save.
enum class Memory {
  Eeprom,
  Sram,
  FlashRam,
  ControllerPak1,
  ControllerPak2,
  ControllerPak3,
  ControllerPak4,
};

inline constexpr std::array<Memory, 7> kAllMemories = {
    Memory::Eeprom,         Memory::Sram,           Memory::FlashRam,       Memory::ControllerPak1,
    Memory::ControllerPak2, Memory::ControllerPak3, Memory::ControllerPak4,
};

// Canonical sizes. EEPROM is always held at 2 KB (the 16 Kbit chip size) because every
// emulator file we target uses that size; a 4 Kbit game only uses the first 512 bytes.
inline constexpr std::size_t kEepromSize = 0x800;
inline constexpr std::size_t kEeprom4kUsed = 0x200;
inline constexpr std::size_t kSramSize = 0x8000;
inline constexpr std::size_t kFlashRamSize = 0x20000;
inline constexpr std::size_t kControllerPakSize = 0x8000;

std::size_t memory_size(Memory memory);

// Byte used for memory the game never wrote. Matches what mupen64plus formats a fresh
// EEPROM, SRAM or FlashRAM with. A Controller Pak has no uniform blank state; 0x00 gives an
// unformatted pak, which games offer to format.
std::uint8_t blank_fill(Memory memory);

// Stable lowercase identifier used in file names and info.json ("sram", "controller_pak_1").
std::string_view memory_key(Memory memory);

bool is_controller_pak(Memory memory);

// The canonical, emulator-independent form of a game's in-game saves.
// Every buffer is in the N64's own byte order (big-endian, as a cartridge reader would dump
// it) and is exactly memory_size() long. Memories the save does not use are simply absent.
struct SaveSet {
  std::map<Memory, Bytes> memories;

  bool operator==(const SaveSet&) const = default;
};

}  // namespace savekit
