// The event gifts the station can hand out: for each one, the Wonder Card the game shows and keeps,
// and the script its Pokémon Center deliveryman runs to hand over the gift. Both are written here,
// in the games' own formats and character set, with the app's own wording; the effect is what the
// events did (the item, and the ship to the island it opens), or what the games were made to
// receive but never did outside Japan (the Altering Cave's other Pokémon).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace p64 {

struct EventGift {
  std::string key;     // "aurora", "mystic", "oldseamap", "eon", "altering-mareep"...
  std::string name;    // "Aurora Ticket"
  std::string where;   // what it opens: "Birth Island (Deoxys)"
  // Gifts that are choices of one event share a group, shown as one row with the choice to pick
  // ("Altering Cave": "Mareep", "Pineco"...). Empty for a gift of its own.
  std::string group, choice;
  uint16_t card_flag;  // the Wonder Card's id in the game (1000 + its gift slot)
  std::vector<uint8_t> card;    // struct WonderCard, 332 bytes
  std::vector<uint8_t> script;  // the deliveryman's script; for a cable event, the event's data
  // Ruby and Sapphire: a Mystery Event, sent over the link cable as the e-Reader did
  // (cable_station.hpp), instead of a Wonder Card over the Wireless Adapter.
  bool cable = false;
  // Emerald's Visiting Trainer: once the game holds this card and its survey (the questionnaire in
  // a Poké Mart) reads these four easy chat words, the station sends this e-Reader trainer.
  std::vector<uint16_t> password = {};
  std::vector<uint8_t> trainer = {};
  // A Stamp Card: once the game holds it, each visit stamps the next of these (species | id << 16).
  std::vector<uint32_t> stamps = {};
};

// The gifts a game can receive, by the game code in its header ("BPEE": Emerald, "BPRE" FireRed,
// "BPGE" LeafGreen, "AXVE" Ruby, "AXPE" Sapphire; English). Empty for other games.
std::vector<EventGift> event_gifts(const std::string& game_code);

// Ruby and Sapphire: the WISHMKR Jirachi event, for the seed the Colosseum Bonus Disc took from the
// save (the "jirachi" gift is made for seed 0; the station makes it for the player's save).
std::vector<uint8_t> jirachi_event(uint16_t seed);

// What the Jirachi event reads from a Ruby or Sapphire cartridge save (read only): the seed (the
// checksum of the newest save's first section), and whether its Pokédex has Jirachi caught.
struct RubySapphireSave {
  bool valid = false;
  uint16_t seed = 0;
  bool has_jirachi = false;
};
RubySapphireSave read_ruby_sapphire_save(const uint8_t* save, std::size_t size);

// Text in the games' character set (English), with \n a new line and \f a new box. {PLAYER} is
// the player's name. Ends with the terminator.
std::vector<uint8_t> encode_game_text(const std::string& text);

// Game Freak's CRC-16 (CalcCRC16WithTable): what Mystery Gift transfers and RAM scripts carry.
uint16_t game_crc16(const uint8_t* data, std::size_t size);

}  // namespace p64
