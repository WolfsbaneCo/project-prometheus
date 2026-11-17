// The in-game save of a handheld game (GBA or Game Boy), between its emulator core and the library.
// A GBA core holds the cartridge's save as one 128 KB block; the Game Boy core holds the cartridge's
// battery memory (as large as the cartridge's RAM) and, for a cartridge with a clock, the clock.
#pragma once

#include <cstdint>
#include <vector>

#include "core.hpp"
#include "library.hpp"
#include "savekit/save_set.hpp"

namespace p64::handheld {

bool is_handheld(System s);

// The save as the core holds it now, in one buffer to compare from one moment to the next (Game Boy:
// the cartridge memory, then the clock). Empty when the game has no save.
std::vector<uint8_t> snapshot(Core& core, System s);
// A save the game never wrote (GBA: all 0xFF; Game Boy: all 0xFF or all 0x00): nothing to keep.
bool blank(System s, const std::vector<uint8_t>& snap, Core& core);
// The library's form of a snapshot.
savekit::SaveSet to_save(System s, const std::vector<uint8_t>& snap, Core& core);
// Puts a library save into the core (right after the game loads). False when it holds none for it.
bool restore(System s, const savekit::SaveSet& save, Core& core);

}  // namespace p64::handheld
