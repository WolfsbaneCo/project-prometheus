// Pokémon stats (src/pokemon_stats.cpp): a party Pokémon built here, encrypted as the games keep
// it, read back through a stand-in for the game's memory.
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "pokemon_stats.hpp"
using namespace p64;
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

namespace {

// Emerald's party in memory (the stand-in only holds these bytes).
constexpr uint32_t kCount = 0x020244E9, kParty = 0x020244EC;

struct Mon {
  uint32_t personality, ot_id;
  uint16_t species;  // as the game numbers it
  uint32_t exp;
  uint8_t evs[6];    // as stored: HP, Attack, Defense, Speed, Sp. Atk, Sp. Def
  uint32_t ivs;      // 5 bits each, in the same order; bit 30: egg
  uint8_t level;
  bool egg = false;
};

void put16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, 2); }
void put32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, 4); }

// 100 bytes, as gPlayerParty holds a Pokémon (pret: struct Pokemon, struct BoxPokemon).
std::vector<uint8_t> build(const Mon& m) {
  std::vector<uint8_t> out(100, 0);
  put32(&out[0], m.personality);
  put32(&out[4], m.ot_id);
  const uint8_t name[10] = {0xCA, 0xC3, 0xC5, 0xBB, 0xBD, 0xC2, 0xCF, 0xFF, 0, 0};  // PIKACHU, in the games' letters
  std::memcpy(&out[8], name, 10);
  out[19] = static_cast<uint8_t>(0x02 | (m.egg ? 0x04 : 0));  // hasSpecies, isEgg
  uint8_t blocks[4][12] = {};
  put16(blocks[0], m.species);
  put32(blocks[0] + 4, m.exp);
  std::memcpy(blocks[2], m.evs, 6);
  put32(blocks[3] + 4, m.ivs);
  const char* orders[24] = {"GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME", "AEGM", "AEMG", "AMGE", "AMEG",
                            "EGAM", "EGMA", "EAGM", "EAMG", "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"};
  uint8_t data[48];
  for (int i = 0; i < 4; ++i) {
    const char* kinds = "GAEM";
    std::memcpy(data + 12 * i, blocks[std::strchr(kinds, orders[m.personality % 24][i]) - kinds], 12);
  }
  uint16_t sum = 0;
  for (int i = 0; i < 48; i += 2) sum = static_cast<uint16_t>(sum + (data[i] | data[i + 1] << 8));
  put16(&out[28], sum);
  const uint32_t key = m.personality ^ m.ot_id;
  for (int i = 0; i < 48; i += 4) {
    uint32_t w;
    std::memcpy(&w, data + i, 4);
    put32(&out[32 + i], w ^ key);
  }
  out[84] = m.level;
  return out;
}

std::vector<OwnedPokemon> read(const std::vector<std::vector<uint8_t>>& party, uint8_t count) {
  std::vector<uint8_t> mem(6 * 100, 0);
  for (std::size_t i = 0; i < party.size(); ++i) std::memcpy(&mem[i * 100], party[i].data(), 100);
  const GbaMemory memory = [&](uint32_t a, std::size_t n) -> const uint8_t* {
    if (a == kCount && n == 1) return &count;
    if (a >= kParty && a + n <= kParty + mem.size()) return &mem[a - kParty];
    return nullptr;
  };
  return party_pokemon("BPEE", memory);
}

}  // namespace

int main() {
  // Pikachu (game number 25), Lv. 50, Adamant (personality % 25 = 3), every block order checked.
  for (uint32_t order = 0; order < 24; ++order) {
    uint32_t pid = 0x12345600;
    while (pid % 24 != order || pid % 25 != 3) ++pid;
    const Mon m{pid, 0xBEEF1234, 25, 125000, {4, 252, 0, 252, 0, 2}, 31u | 30u << 5 | 29u << 10 | 28u << 15 | 27u << 20 | 26u << 25, 50};
    const auto got = read({build(m)}, 1);
    CHECK(got.size() == 1);
    if (got.size() != 1) continue;
    const OwnedPokemon& p = got[0];
    CHECK(p.species == "Pikachu" && p.dex == 25 && p.nickname == "PIKACHU" && p.level == 50 && p.nature == "Adamant");
    // Shown as HP, Attack, Defense, Sp. Atk, Sp. Def, Speed.
    CHECK(p.evs[0] == 4 && p.evs[1] == 252 && p.evs[2] == 0 && p.evs[3] == 0 && p.evs[4] == 2 && p.evs[5] == 252);
    CHECK(p.ev_total() == 510);
    CHECK(p.ivs[0] == 31 && p.ivs[1] == 30 && p.ivs[2] == 29 && p.ivs[3] == 27 && p.ivs[4] == 26 && p.ivs[5] == 28);
    CHECK(p.nature_up == 1 && p.nature_down == 3);  // Adamant: + Attack, - Sp. Atk
  }
  // A Hoenn Pokémon: the game numbers Treecko 277, the Pokédex 252.
  Mon treecko{0x00000006, 0x1, 277, 1000, {}, 0, 0};  // Docile (6 % 25: neutral); no level byte, so from experience
  auto got = read({build(treecko)}, 1);
  CHECK(got.size() == 1 && got[0].species == "Treecko" && got[0].dex == 252 && got[0].nature == "Docile");
  CHECK(got.size() == 1 && got[0].nature_up == -1 && got[0].nature_down == -1);
  CHECK(got.size() == 1 && got[0].level == 12);  // Medium Slow: 973 experience reaches 12, 1261 reaches 13
  // An egg, a damaged Pokémon (checksum off) and an empty party are left out; the others stay.
  Mon egg = treecko;
  egg.egg = true;
  std::vector<uint8_t> broken = build(treecko);
  broken[40] ^= 0x10;
  got = read({build(egg), broken, build(treecko)}, 3);
  CHECK(got.size() == 1);
  CHECK(read({build(treecko)}, 0).empty());
  CHECK(read({build(treecko)}, 7).empty());  // a party count that can't be: nothing
  std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails != 0;
}
