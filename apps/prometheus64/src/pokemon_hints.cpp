#include "pokemon_hints.hpp"

#include <cstring>

namespace p64 {

namespace {

// Where each game keeps what the hints read. Emerald moves its save blocks around in memory and
// points to them; Ruby and Sapphire keep them at fixed addresses (the same in Ruby 1.0 and
// Sapphire 1.2, checked against both games' code).
struct Layout {
  bool pointers;           // save block 1 and the PC through pointers (Emerald) or at fixed addresses
  uint32_t save_block1;    // gSaveBlock1Ptr, or gSaveBlock1
  uint32_t storage;        // gPokemonStoragePtr, or gPokemonStorage
  uint32_t party_count;    // gPlayerPartyCount
  uint32_t party;          // gPlayerParty: 6 x struct Pokemon (100 bytes)
  uint32_t flags, vars;    // SaveBlock1.flags, .vars (from VARS_START, 0x4000)
  uint16_t lottery_flag;   // FLAG_DAILY_PICKED_LOTO(TO)_TICKET
};
constexpr Layout kEmerald = {true, 0x03005D8C, 0x03005D94, 0x020244E9, 0x020244EC, 0x1270, 0x139C, 0x92A};
constexpr Layout kRubySapphire = {false, 0x02025734, 0x020300A0, 0x03004350, 0x03004360, 0x1220, 0x1340, 0x8CA};
constexpr uint16_t kVarMirageRndH = 0x4024;        // the same in all three
constexpr uint16_t kVarLotteryRnd1 = 0x404B;       // the low half: the number drawn against
constexpr int kBoxes = 14, kBoxSlots = 30;

const Layout* layout_for(const std::string& game_code) {
  if (game_code == "BPEE") return &kEmerald;
  if (game_code == "AXVE" || game_code == "AXPE") return &kRubySapphire;
  return nullptr;
}
constexpr uint32_t kPartyMonSize = 100, kBoxMonSize = 80;

bool in_ewram(uint32_t address, uint32_t size) { return address >= 0x02000000 && address + size <= 0x02040000; }
bool in_iwram(uint32_t address, uint32_t size) { return address >= 0x03000000 && address + size <= 0x03008000; }

uint32_t read32(const GbaMemory& memory, uint32_t address) {
  uint32_t v = 0;
  if (const uint8_t* p = memory(address, 4)) std::memcpy(&v, p, 4);
  return v;
}

// The start of a Pokémon (party or box), which the game keeps unencrypted: personality, original
// trainer ID, nickname, and the "has a species" and "is an egg" bits.
struct MonHead {
  uint32_t personality = 0, ot_id = 0;
  std::string nickname;
  bool present = false, egg = false;
};

std::string decode_name(const uint8_t* text, std::size_t size) {
  std::string out;
  for (std::size_t i = 0; i < size && text[i] != 0xFF; ++i) {
    const uint8_t c = text[i];
    if (c >= 0xBB && c <= 0xD4) out += static_cast<char>('A' + (c - 0xBB));
    else if (c >= 0xD5 && c <= 0xEE) out += static_cast<char>('a' + (c - 0xD5));
    else if (c >= 0xA1 && c <= 0xAA) out += static_cast<char>('0' + (c - 0xA1));
    else if (c == 0x00) out += ' ';
    else if (c == 0xAE) out += '-';
    else if (c == 0xAD) out += '.';
    else if (c == 0x1B) out += "\xC3\xA9";  // é
  }
  return out;
}

MonHead read_mon(const GbaMemory& memory, uint32_t address) {
  MonHead m;
  const uint8_t* p = memory(address, 20);
  if (!p) return m;
  std::memcpy(&m.personality, p, 4);
  std::memcpy(&m.ot_id, p + 4, 4);
  m.nickname = decode_name(p + 8, 10);
  m.present = (p[19] & 0x02) != 0;  // hasSpecies
  m.egg = (p[19] & 0x04) != 0;      // isEgg
  return m;
}

// Matching decimal digits from the right, as the Lottery Corner counts them (up to 5).
int matching_digits(uint32_t win, uint32_t id) {
  int n = 0;
  for (int i = 0; i < 5 && win % 10 == id % 10; ++i, win /= 10, id /= 10) ++n;
  return n;
}

}  // namespace

bool has_daily_hints(const std::string& game_code) { return layout_for(game_code) != nullptr; }

std::vector<DailyHint> daily_hints(const std::string& game_code, const GbaMemory& memory) {
  std::vector<DailyHint> out;
  const Layout* l = layout_for(game_code);
  if (!l) return out;
  const uint32_t sb1 = l->pointers ? read32(memory, l->save_block1) : l->save_block1;
  const uint32_t storage = l->pointers ? read32(memory, l->storage) : l->storage;
  const uint8_t* count_p = memory(l->party_count, 1);
  if (!in_ewram(sb1, l->vars + 0x200) || !in_ewram(storage, 4 + kBoxes * kBoxSlots * kBoxMonSize) || !count_p ||
      !(in_ewram(l->party, 6 * kPartyMonSize) || in_iwram(l->party, 6 * kPartyMonSize))) {
    return out;
  }
  const int party_count = *count_p <= 6 ? *count_p : 0;
  auto var = [&](uint16_t id) -> uint16_t {
    uint16_t v = 0;
    if (const uint8_t* p = memory(sb1 + l->vars + 2u * (id - 0x4000u), 2)) std::memcpy(&v, p, 2);
    return v;
  };
  auto flag = [&](uint16_t id) {
    const uint8_t* p = memory(sb1 + l->flags + id / 8u, 1);
    return p && ((*p >> (id % 8)) & 1);
  };

  std::vector<MonHead> party;
  for (int i = 0; i < party_count; ++i) party.push_back(read_mon(memory, l->party + i * kPartyMonSize));

  // Mirage Island: any party Pokémon (eggs too, as the game checks only the species). The game
  // looks at the party only; a matching Pokémon in the PC is worth knowing about too: moved into
  // the party, it brings the island out the same day. Either way the hint is told once a day.
  const uint16_t mirage = var(kVarMirageRndH);
  bool mirage_told = false;
  for (const MonHead& m : party) {
    if (m.present && (m.personality & 0xFFFF) == mirage) {
      out.push_back({"mirage:" + std::to_string(mirage),
                     "Mirage Island is out today! Sail to Route 130, west of Pacifidlog Town, with " + m.nickname +
                         " in your party. It's gone when the day changes."});
      mirage_told = true;
      break;
    }
  }
  for (int b = 0; b < kBoxes && !mirage_told; ++b) {
    for (int s = 0; s < kBoxSlots && !mirage_told; ++s) {
      const MonHead m = read_mon(memory, storage + 4 + (b * kBoxSlots + s) * kBoxMonSize);
      if (!m.present || (m.personality & 0xFFFF) != mirage) continue;
      // struct PokemonStorage: the box names follow the boxes, 9 bytes each ("BOX 1" unless renamed).
      std::string box = "Box " + std::to_string(b + 1);
      if (const uint8_t* name = memory(storage + 4 + kBoxes * kBoxSlots * kBoxMonSize + 9u * b, 9)) {
        std::string named = decode_name(name, 9);
        named.erase(named.find_last_not_of(' ') + 1);
        named.erase(0, named.find_first_not_of(' '));
        if (!named.empty()) box = named;
      }
      out.push_back({"mirage:" + std::to_string(mirage),
                     "Mirage Island could be out today: " + m.nickname + ", in your PC (" + box +
                         "), brings it out. Put " + m.nickname +
                         " in your party and sail to Route 130, west of Pacifidlog Town. It's gone when the day changes."});
      mirage_told = true;
    }
  }

  // Lottery Corner: the best match among the party and the PC, not eggs; one ticket a day.
  if (!flag(l->lottery_flag)) {
    const uint16_t win = var(kVarLotteryRnd1);
    int best = 0;
    std::string who;
    auto consider = [&](const MonHead& m) {
      if (!m.present || m.egg) return;
      const int n = matching_digits(win, m.ot_id & 0xFFFF);
      if (n > best && n > 1) {
        best = n;
        who = m.nickname;
      }
    };
    for (const MonHead& m : party) consider(m);
    for (int b = 0; b < kBoxes; ++b) {
      for (int s = 0; s < kBoxSlots; ++s) consider(read_mon(memory, storage + 4 + (b * kBoxSlots + s) * kBoxMonSize));
    }
    if (best >= 2) {
      static const char* const kPrizes[] = {"PP Up", "Exp. Share", "Max Revive", "Master Ball"};
      out.push_back({"lottery:" + std::to_string(win),
                     "Lucky day at the Lottery Corner: today's number matches " + std::to_string(best) + " digits of " + who +
                         "'s ID. Draw a ticket at the Lilycove Department Store to win a " + kPrizes[best - 2] + "."});
    }
  }
  return out;
}

}  // namespace p64
