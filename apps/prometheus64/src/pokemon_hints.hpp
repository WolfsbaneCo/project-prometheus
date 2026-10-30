// Daily hints for the Pokémon games' luck events: the app reads the running game, the way a guide
// would, and says so on the days the player would be lucky. It never writes to the game or its
// save, and never changes the odds.
//   - Mirage Island (Ruby, Sapphire, Emerald): seen from Route 130 on days when a daily number
//     matches the low half of a party Pokémon's personality value (IsMirageIslandPresent).
//   - Lottery Corner (the same games, Lilycove Department Store): the day's number against each
//     Pokémon's trainer ID, party and PC, last digits first (lottery_corner.c,
//     PickLotteryCornerTicket); 2 digits or more win, and only one ticket a day.
// Written from the pret decompilations of Emerald and Ruby/Sapphire (English: BPEE, AXVE, AXPE);
// addresses checked against the games' own code, which refers to them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace p64 {

struct DailyHint {
  std::string key;   // the same luck on the same day has the same key: shown once
  std::string text;
};

// The game's memory as the console sees it (nullptr where it isn't mapped).
using GbaMemory = std::function<const uint8_t*(uint32_t address, std::size_t size)>;

// Whether this game (by its four-letter code) has daily hints.
bool has_daily_hints(const std::string& game_code);
// Today's hints, or none while no save is loaded (title screen, intro).
std::vector<DailyHint> daily_hints(const std::string& game_code, const GbaMemory& memory);

}  // namespace p64
