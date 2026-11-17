#include "handheld_save.hpp"

#include <algorithm>
#include <cstring>

namespace p64::handheld {

bool is_handheld(System s) { return s == System::GBA || s == System::GB; }

std::vector<uint8_t> snapshot(Core& core, System s) {
  std::vector<uint8_t> out;
  uint8_t* ram = core.save_ram();
  const std::size_t n = core.save_ram_size();
  if (s == System::GBA) {
    if (ram && n == savekit::kGbaSaveSize) out.assign(ram, ram + n);
    return out;
  }
  if (s != System::GB) return out;
  if (ram && n > 0 && n <= savekit::kGbSaveSize) out.assign(ram, ram + n);
  if (out.empty()) return out;  // a clock without battery memory has nothing to keep
  if (uint8_t* rtc = core.rtc(); rtc && core.rtc_size() == savekit::kGbRtcSize) out.insert(out.end(), rtc, rtc + savekit::kGbRtcSize);
  return out;
}

bool blank(System s, const std::vector<uint8_t>& snap, Core& core) {
  if (snap.empty()) return true;
  const std::size_t ram = s == System::GB ? std::min(snap.size(), core.save_ram_size()) : snap.size();
  const uint8_t first = snap[0];
  return (first == 0xFF || first == 0x00) &&
         std::all_of(snap.begin(), snap.begin() + static_cast<std::ptrdiff_t>(ram), [first](uint8_t b) { return b == first; });
}

savekit::SaveSet to_save(System s, const std::vector<uint8_t>& snap, Core& core) {
  savekit::SaveSet out;
  if (snap.empty()) return out;
  if (s == System::GBA) {
    out.memories[savekit::Memory::GbaSave] = savekit::Bytes(snap);
    return out;
  }
  const std::size_t n = std::min(snap.size(), core.save_ram_size());
  savekit::Bytes ram(snap.begin(), snap.begin() + static_cast<std::ptrdiff_t>(n));
  ram.resize(savekit::kGbSaveSize, 0xFF);
  out.memories[savekit::Memory::GbSave] = std::move(ram);
  if (snap.size() == n + savekit::kGbRtcSize) {
    out.memories[savekit::Memory::GbRtc] = savekit::Bytes(snap.begin() + static_cast<std::ptrdiff_t>(n), snap.end());
  }
  return out;
}

bool restore(System s, const savekit::SaveSet& save, Core& core) {
  uint8_t* ram = core.save_ram();
  const std::size_t n = core.save_ram_size();
  if (s == System::GBA) {
    auto it = save.memories.find(savekit::Memory::GbaSave);
    if (!ram || n != savekit::kGbaSaveSize || it == save.memories.end() || it->second.size() != n) return false;
    std::memcpy(ram, it->second.data(), n);
    return true;
  }
  auto it = save.memories.find(savekit::Memory::GbSave);
  if (s != System::GB || !ram || n == 0 || n > savekit::kGbSaveSize || it == save.memories.end() || it->second.size() < n) return false;
  std::memcpy(ram, it->second.data(), n);
  auto clock = save.memories.find(savekit::Memory::GbRtc);
  if (uint8_t* rtc = core.rtc(); rtc && core.rtc_size() == savekit::kGbRtcSize && clock != save.memories.end() &&
                                 clock->second.size() == savekit::kGbRtcSize) {
    std::memcpy(rtc, clock->second.data(), savekit::kGbRtcSize);
  }
  return true;
}

}  // namespace p64::handheld
