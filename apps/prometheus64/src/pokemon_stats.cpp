#include "pokemon_stats.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace p64 {

namespace {

struct SpeciesInfo {
  const char* name;
  int dex;
};
#include "pokemon_species.inc"  // kSpecies, kGrowth

// Where each game keeps its party. The same addresses as the daily hints (pokemon_hints.cpp) for
// Ruby, Sapphire and Emerald; FireRed and LeafGreen (1.0 and 1.1) share theirs (pret pokefirered).
struct Layout {
  uint32_t party_count;  // gPlayerPartyCount
  uint32_t party;        // gPlayerParty: 6 x struct Pokemon (100 bytes)
};
constexpr Layout kEmerald = {0x020244E9, 0x020244EC};
constexpr Layout kRubySapphire = {0x03004350, 0x03004360};
constexpr Layout kFireRedLeafGreen = {0x02024029, 0x02024284};

const Layout* layout_for(const std::string& code) {
  if (code == "BPEE") return &kEmerald;
  if (code == "AXVE" || code == "AXPE") return &kRubySapphire;
  if (code == "BPRE" || code == "BPGE") return &kFireRedLeafGreen;
  return nullptr;
}

constexpr uint32_t kPartyMonSize = 100;

bool in_ewram(uint32_t address, uint32_t size) { return address >= 0x02000000 && address + size <= 0x02040000; }
bool in_iwram(uint32_t address, uint32_t size) { return address >= 0x03000000 && address + size <= 0x03008000; }

const char* const kNatures[25] = {"Hardy", "Lonely", "Brave",   "Adamant", "Naughty", "Bold",    "Docile",
                                  "Relaxed", "Impish", "Lax",   "Timid",   "Hasty",   "Serious", "Jolly",
                                  "Naive", "Modest", "Mild",    "Quiet",   "Bashful", "Rash",    "Calm",
                                  "Gentle", "Sassy", "Careful", "Quirky"};

// The games' own text, as far as names go (pret charmap.txt). The male and female signs aren't in
// the app's font: written as M and F, as the species table does.
std::string decode_text(const uint8_t* text, std::size_t size) {
  std::string out;
  for (std::size_t i = 0; i < size && text[i] != 0xFF; ++i) {
    const uint8_t c = text[i];
    if (c >= 0xBB && c <= 0xD4) out += static_cast<char>('A' + (c - 0xBB));
    else if (c >= 0xD5 && c <= 0xEE) out += static_cast<char>('a' + (c - 0xD5));
    else if (c >= 0xA1 && c <= 0xAA) out += static_cast<char>('0' + (c - 0xA1));
    else if (c == 0x00) out += ' ';
    else if (c == 0xAB) out += '!';
    else if (c == 0xAC) out += '?';
    else if (c == 0xAD) out += '.';
    else if (c == 0xAE) out += '-';
    else if (c == 0xB0) out += "...";
    else if (c == 0xB1 || c == 0xB2) out += '"';
    else if (c == 0xB3 || c == 0xB4) out += '\'';
    else if (c == 0xB5) out += " M";
    else if (c == 0xB6) out += " F";
    else if (c == 0xB8) out += ',';
    else if (c == 0xBA) out += '/';
    else if (c == 0x1B) out += "\xC3\xA9";  // é
  }
  const auto end = out.find_last_not_of(' ');
  out.erase(end == std::string::npos ? 0 : end + 1);
  return out;
}

// The experience a growth rate needs for a level (pret experience_tables.h).
uint32_t exp_for(int growth, int n) {
  if (n <= 1) return n <= 0 ? 0 : 1;
  const int64_t c = static_cast<int64_t>(n) * n * n;
  int64_t e = c;
  switch (growth) {
    case 1:  // Erratic
      e = n <= 50 ? (100 - n) * c / 50 : n <= 68 ? (150 - n) * c / 100 : n <= 98 ? ((1911 - 10 * n) / 3) * c / 500 : (160 - n) * c / 100;
      break;
    case 2:  // Fluctuating
      e = n <= 15 ? ((n + 1) / 3 + 24) * c / 50 : n <= 36 ? (n + 14) * c / 50 : ((n / 2) + 32) * c / 50;
      break;
    case 3: e = 6 * c / 5 - 15 * static_cast<int64_t>(n) * n + 100 * n - 140; break;  // Medium Slow
    case 4: e = 4 * c / 5; break;  // Fast
    case 5: e = 5 * c / 4; break;  // Slow
    default: break;                // Medium Fast
  }
  return static_cast<uint32_t>(e);
}

int level_for(int growth, uint32_t exp) {
  int level = 1;
  while (level < 100 && exp_for(growth, level + 1) <= exp) ++level;
  return level;
}

// One Pokémon (its first 80 bytes, which a party Pokémon shares with a PC one), decrypted and checked. False for an
// empty slot, an egg, or data that fails its checksum.
bool decode_mon(const uint8_t* p, OwnedPokemon& out) {
  if (!(p[19] & 0x02) || (p[19] & 0x04)) return false;  // hasSpecies, isEgg
  uint32_t personality, ot_id;
  std::memcpy(&personality, p, 4);
  std::memcpy(&ot_id, p + 4, 4);
  const uint32_t key = personality ^ ot_id;
  uint8_t data[48];
  for (int i = 0; i < 48; i += 4) {
    uint32_t w;
    std::memcpy(&w, p + 32 + i, 4);
    w ^= key;
    std::memcpy(data + i, &w, 4);
  }
  uint16_t sum = 0, checksum;
  for (int i = 0; i < 48; i += 2) sum = static_cast<uint16_t>(sum + (data[i] | data[i + 1] << 8));
  std::memcpy(&checksum, p + 28, 2);
  if (sum != checksum) return false;
  // The four blocks (Growth, Attacks, EVs, Misc) in one of 24 orders, by personality % 24.
  static const char* const kOrders[24] = {"GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME",
                                          "AEGM", "AEMG", "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG",
                                          "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"};
  const char* order = kOrders[personality % 24];
  auto block = [&](char which) { return data + 12 * (std::strchr(order, which) - order); };
  const uint8_t* growth = block('G');
  const uint8_t* evs = block('E');
  const uint8_t* misc = block('M');
  const int species = growth[0] | growth[1] << 8;
  if (species < 1 || species > 411 || kSpecies[species - 1].dex == 0) return false;
  out.species = kSpecies[species - 1].name;
  out.dex = kSpecies[species - 1].dex;
  out.nickname = decode_text(p + 8, 10);
  uint32_t exp, ivs;
  std::memcpy(&exp, growth + 4, 4);
  std::memcpy(&ivs, misc + 4, 4);
  if (ivs & (1u << 30)) return false;  // an egg, by its own flag
  out.level = level_for(kGrowth[species - 1], exp);
  const int nature = static_cast<int>(personality % 25);
  out.nature = kNatures[nature];
  // A nature raises one stat by a tenth and lowers another, in the order Attack, Defense, Speed,
  // Sp. Atk, Sp. Def (the same stat both ways: neither).
  static const int kNatureStat[5] = {1, 2, 5, 3, 4};
  if (nature / 5 != nature % 5) {
    out.nature_up = kNatureStat[nature / 5];
    out.nature_down = kNatureStat[nature % 5];
  }
  // Stored HP, Attack, Defense, Speed, Sp. Atk, Sp. Def; shown with Speed last.
  static const int kShown[6] = {0, 1, 2, 4, 5, 3};
  for (int i = 0; i < 6; ++i) {
    out.evs[i] = evs[kShown[i]];
    out.ivs[i] = static_cast<int>((ivs >> (5 * kShown[i])) & 31);
  }
  return true;
}

}  // namespace

bool has_pokemon_stats(const std::string& game_code) { return layout_for(game_code) != nullptr; }

std::vector<OwnedPokemon> party_pokemon(const std::string& game_code, const GbaMemory& memory) {
  std::vector<OwnedPokemon> out;
  const Layout* l = layout_for(game_code);
  if (!l) return out;
  const uint8_t* count = memory(l->party_count, 1);
  if (!count || !(in_ewram(l->party, 6 * kPartyMonSize) || in_iwram(l->party, 6 * kPartyMonSize))) return out;
  const int party = *count <= 6 ? *count : 0;
  for (int i = 0; i < party; ++i) {
    const uint8_t* p = memory(l->party + static_cast<uint32_t>(i) * kPartyMonSize, kPartyMonSize);
    OwnedPokemon m;
    if (!p || !decode_mon(p, m)) continue;
    if (p[84] >= 1 && p[84] <= 100) m.level = p[84];  // the level the game shows
    out.push_back(std::move(m));
  }
  return out;
}

}  // namespace p64
