// Pokémon stats (EVs): the in-game menu's page for the player's party in the generation 3 games
// (Ruby, Sapphire, Emerald, FireRed, LeafGreen). The player picks a party Pokémon and sees how its
// effort values (EVs) are spread, with its individual values (IVs) and nature. The app reads the
// running game's party, the way the game's own summary screen does, only when the page is open,
// and never writes to the game or its save.
//
// Each Pokémon's stats are kept encrypted in four blocks whose order depends on its personality
// value; they're decrypted with the personality and the trainer ID and checked against the
// Pokémon's own checksum, as the games do (pret decompilations: pokemon.c, GetBoxMonData).
#pragma once

#include <array>
#include <string>
#include <vector>

#include "pokemon_hints.hpp"  // GbaMemory

namespace p64 {

struct OwnedPokemon {
  std::string species;   // "Pikachu"
  int dex = 0;           // National Pokédex number
  std::string nickname;  // as the game shows it (the species name, in capitals, unless renamed)
  int level = 0;
  // Its nature, and the stats the nature raises and lowers (indices into evs; -1: neither).
  std::string nature;
  int nature_up = -1, nature_down = -1;
  // HP, Attack, Defense, Sp. Atk, Sp. Def, Speed (the summary screen's order).
  std::array<int, 6> evs{}, ivs{};
  int ev_total() const { return evs[0] + evs[1] + evs[2] + evs[3] + evs[4] + evs[5]; }
};

// Whether this game (its four-letter code) has the page.
bool has_pokemon_stats(const std::string& game_code);
// The party, in order (eggs left out), or nothing before a save is loaded.
std::vector<OwnedPokemon> party_pokemon(const std::string& game_code, const GbaMemory& memory);

// The most EVs a Pokémon can have in all (and in one stat: 255).
constexpr int kMaxEvTotal = 510;

}  // namespace p64
