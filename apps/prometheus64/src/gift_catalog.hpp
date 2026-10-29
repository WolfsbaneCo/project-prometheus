// The event gifts the station can hand out: for each one, the Wonder Card the game shows and keeps,
// and the script its Pokémon Center deliveryman runs to hand over the item. Both are written here,
// in the games' own formats and character set, with the app's own wording; the effect is what the
// events did (the item, and the ship to the island it opens).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace p64 {

struct EventGift {
  std::string key;     // "aurora", "mystic", "oldseamap", "eon"
  std::string name;    // "Aurora Ticket"
  std::string where;   // what it opens: "Birth Island (Deoxys)"
  uint16_t card_flag;  // the Wonder Card's id in the game (1000 + its gift slot)
  std::vector<uint8_t> card;    // struct WonderCard, 332 bytes
  std::vector<uint8_t> script;  // the deliveryman's script
};

// The gifts a game can receive, by the game code in its header ("BPEE": Emerald, English).
// Empty for games without wireless Mystery Gift.
std::vector<EventGift> event_gifts(const std::string& game_code);

// Text in the games' character set (English), with \n a new line and \f a new box. {PLAYER} is
// the player's name. Ends with the terminator.
std::vector<uint8_t> encode_game_text(const std::string& text);

// Game Freak's CRC-16 (CalcCRC16WithTable): what Mystery Gift transfers and RAM scripts carry.
uint16_t game_crc16(const uint8_t* data, std::size_t size);

}  // namespace p64
