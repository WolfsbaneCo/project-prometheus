#include "pokemon_hints.hpp"

#include <cstring>

namespace p64 {

namespace {

// Pokémon Emerald (English) in memory.
constexpr uint32_t kSaveBlock1Ptr = 0x03005D8C;    // gSaveBlock1Ptr
constexpr uint32_t kStoragePtr = 0x03005D94;       // gPokemonStoragePtr
constexpr uint32_t kPartyCount = 0x020244E9;       // gPlayerPartyCount
constexpr uint32_t kParty = 0x020244EC;            // gPlayerParty: 6 x struct Pokemon (100 bytes)
constexpr uint32_t kFlagsOffset = 0x1270;          // SaveBlock1.flags
constexpr uint32_t kVarsOffset = 0x139C;           // SaveBlock1.vars, from VARS_START (0x4000)
constexpr uint16_t kVarMirageRndH = 0x4024;
constexpr uint16_t kVarLotteryRnd1 = 0x404B;       // the low half: the number drawn against
constexpr uint16_t kFlagPickedLotteryTicket = 0x92A;  // FLAG_DAILY_PICKED_LOTO_TICKET
constexpr int kBoxes = 14, kBoxSlots = 30;
constexpr uint32_t kPartyMonSize = 100, kBoxMonSize = 80;

bool in_ewram(uint32_t address, uint32_t size) { return address >= 0x02000000 && address + size <= 0x02040000; }

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

bool has_daily_hints(const std::string& game_code) { return game_code == "BPEE"; }

std::vector<DailyHint> daily_hints(const std::string& game_code, const GbaMemory& memory) {
  std::vector<DailyHint> out;
  if (!has_daily_hints(game_code)) return out;
  const uint32_t sb1 = read32(memory, kSaveBlock1Ptr), storage = read32(memory, kStoragePtr);
  const uint8_t* count_p = memory(kPartyCount, 1);
  if (!in_ewram(sb1, kVarsOffset + 0x200) || !in_ewram(storage, 4 + kBoxes * kBoxSlots * kBoxMonSize) || !count_p) return out;
  const int party_count = *count_p <= 6 ? *count_p : 0;
  auto var = [&](uint16_t id) -> uint16_t {
    uint16_t v = 0;
    if (const uint8_t* p = memory(sb1 + kVarsOffset + 2u * (id - 0x4000u), 2)) std::memcpy(&v, p, 2);
    return v;
  };
  auto flag = [&](uint16_t id) {
    const uint8_t* p = memory(sb1 + kFlagsOffset + id / 8u, 1);
    return p && ((*p >> (id % 8)) & 1);
  };

  std::vector<MonHead> party;
  for (int i = 0; i < party_count; ++i) party.push_back(read_mon(memory, kParty + i * kPartyMonSize));

  // Mirage Island: any party Pokémon (eggs too, as the game checks only the species).
  const uint16_t mirage = var(kVarMirageRndH);
  for (const MonHead& m : party) {
    if (m.present && (m.personality & 0xFFFF) == mirage) {
      out.push_back({"mirage:" + std::to_string(mirage),
                     "Mirage Island is out today! Sail to Route 130, west of Pacifidlog Town, with " + m.nickname +
                         " in your party. It's gone when the day changes."});
      break;
    }
  }

  // Lottery Corner: the best match among the party and the PC, not eggs; one ticket a day.
  if (!flag(kFlagPickedLotteryTicket)) {
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
