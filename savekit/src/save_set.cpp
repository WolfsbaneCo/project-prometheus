#include "savekit/save_set.hpp"

namespace savekit {

std::size_t memory_size(Memory memory) {
  switch (memory) {
    case Memory::Eeprom: return kEepromSize;
    case Memory::Sram: return kSramSize;
    case Memory::FlashRam: return kFlashRamSize;
    case Memory::GbaSave: return kGbaSaveSize;
    case Memory::GbSave: return kGbSaveSize;
    case Memory::GbRtc: return kGbRtcSize;
    default: return kControllerPakSize;
  }
}

std::uint8_t blank_fill(Memory memory) {
  return is_controller_pak(memory) || memory == Memory::GbRtc ? 0x00 : 0xFF;
}

std::string_view memory_key(Memory memory) {
  switch (memory) {
    case Memory::Eeprom: return "eeprom";
    case Memory::Sram: return "sram";
    case Memory::FlashRam: return "flashram";
    case Memory::ControllerPak1: return "controller_pak_1";
    case Memory::ControllerPak2: return "controller_pak_2";
    case Memory::ControllerPak3: return "controller_pak_3";
    case Memory::ControllerPak4: return "controller_pak_4";
    case Memory::GbaSave: return "gba_save";
    case Memory::GbSave: return "gb_save";
    case Memory::GbRtc: return "gb_rtc";
  }
  return "unknown";
}

bool is_controller_pak(Memory memory) {
  return memory == Memory::ControllerPak1 || memory == Memory::ControllerPak2 ||
         memory == Memory::ControllerPak3 || memory == Memory::ControllerPak4;
}

}  // namespace savekit
