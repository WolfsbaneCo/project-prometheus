#include "gift_catalog.hpp"

#include <array>
#include <map>
#include <utility>

namespace p64 {
namespace {

// The deliveryman's script runs from wherever the game keeps it in RAM, so it uses the games'
// relative commands: setvaddress names the address the script assumes it starts at, and every
// v-command's address is taken relative to that (as the events' own scripts did).
class ScriptWriter {
 public:
  static constexpr uint32_t kBase = 0x08F00000;  // any address: only offsets from it matter

  std::vector<uint8_t>& bytes() { return b_; }
  void op(uint8_t v) { b_.push_back(v); }
  void u16(uint16_t v) {
    b_.push_back(static_cast<uint8_t>(v));
    b_.push_back(static_cast<uint8_t>(v >> 8));
  }
  void u32(uint32_t v) {
    u16(static_cast<uint16_t>(v));
    u16(static_cast<uint16_t>(v >> 16));
  }
  void ref(const std::string& label) {
    fixups_.emplace_back(b_.size(), label);
    u32(0);
  }
  void label(const std::string& name) { labels_[name] = b_.size(); }
  void text(const std::string& name, const std::string& s) {
    label(name);
    const std::vector<uint8_t> t = encode_game_text(s);
    b_.insert(b_.end(), t.begin(), t.end());
  }

  // Commands (Emerald's script command table).
  void setvaddress() { op(0xB8), u32(kBase + static_cast<uint32_t>(b_.size()) - 1); }
  void lock() { op(0x6A); }
  void faceplayer() { op(0x5A); }
  void release() { op(0x6C); }
  void end() { op(0x02); }
  void waitmessage() { op(0x66); }
  void waitbuttonpress() { op(0x6D); }
  void setflag(uint16_t flag) { op(0x29), u16(flag); }
  void setvar(uint16_t var, uint16_t value) { op(0x16), u16(var), u16(value); }
  void vmessage(const std::string& text) { op(0xBD), ref(text); }
  void vgoto_if_set(uint16_t flag, const std::string& dest) {
    op(0x2B), u16(flag);           // checkflag
    op(0xBB), op(1), ref(dest);    // vgoto_if EQUAL
  }
  void vgoto_if_result(uint16_t value, const std::string& dest) {
    op(0x21), u16(0x800D), u16(value);  // compare_var_to_value VAR_RESULT, value
    op(0xBB), op(1), ref(dest);         // vgoto_if EQUAL
  }
  void checkitem(uint16_t item) { op(0x47), u16(item), u16(1); }
  void checkitemspace(uint16_t item) { op(0x46), u16(item), u16(1); }
  void giveitem(uint16_t item) {
    op(0x1A), u16(0x8000), u16(item);  // setorcopyvar VAR_0x8000, item
    op(0x1A), u16(0x8001), u16(1);     // setorcopyvar VAR_0x8001, 1
    op(0x09), op(0);                   // callstd STD_OBTAIN_ITEM
  }
  void say(const std::string& text) {
    vmessage(text);
    waitmessage();
    waitbuttonpress();
  }

  std::vector<uint8_t> finish() {
    for (const auto& [at, name] : fixups_) {
      const uint32_t addr = kBase + static_cast<uint32_t>(labels_.at(name));
      for (int i = 0; i < 4; ++i) b_[at + static_cast<std::size_t>(i)] = static_cast<uint8_t>(addr >> (8 * i));
    }
    return b_;
  }

 private:
  std::vector<uint8_t> b_;
  std::map<std::string, std::size_t> labels_;
  std::vector<std::pair<std::size_t, std::string>> fixups_;
};

struct TicketDef {
  const char* key;
  const char* name;
  const char* where;
  uint16_t card_flag;     // 1000 + slot in the game's received-gift flags
  uint16_t item;
  uint16_t ship_flag;     // the harbor's ship to the island
  uint16_t received_flag;
  std::vector<uint16_t> done_flags;  // already caught or met: nothing more to hand over
  uint16_t icon_species;
  uint8_t background;
  const char* item_caps;  // as the games write it: "AURORA TICKET"
  const char* card_lines[4];
};

// Emerald (English). Flags and items from the game's own tables.
const TicketDef kEmerald[] = {
    {"aurora", "Aurora Ticket", "Birth Island, where Deoxys waits", 1000, 0x173, 0x8D5, 0x13A, {0x1AD}, 410, 1,
     "AURORA TICKET",
     {"A ticket for a ship to BIRTH", "ISLAND, far to the south. Pick", "it up from the deliveryman in any",
      "POKéMON CENTER."}},
    {"mystic", "Mystic Ticket", "Navel Rock, where Lugia and Ho-Oh rest", 1001, 0x172, 0x8E0, 0x13B, {0x91, 0x92}, 249,
     2, "MYSTIC TICKET",
     {"A ticket for a ship to NAVEL", "ROCK. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
    {"oldseamap", "Old Sea Map", "Faraway Island, where Mew hides", 1002, 0x178, 0x8D6, 0x13C, {0x1CA}, 151, 3,
     "OLD SEA MAP",
     {"An old map of an island no one", "has charted. Pick it up from the", "deliveryman in any POKéMON",
      "CENTER."}},
    {"eon", "Eon Ticket", "Southern Island, where Latias or Latios lives", 1003, 0x113, 0x8B3, 0x13D, {0x1C9}, 407, 4,
     "EON TICKET",
     {"A ticket for a ship to SOUTHERN", "ISLAND. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
};

std::vector<uint8_t> ticket_script(const TicketDef& t) {
  const std::string item = t.item_caps;
  ScriptWriter s;
  s.setvaddress();
  s.lock();
  s.faceplayer();
  s.vgoto_if_set(t.received_flag, "done");
  for (uint16_t f : t.done_flags) s.vgoto_if_set(f, "done");
  s.checkitem(t.item);
  s.vgoto_if_result(1, "done");
  s.say("thanks");
  s.checkitemspace(t.item);
  s.vgoto_if_result(0, "full");
  s.giveitem(t.item);
  s.setflag(t.ship_flag);
  s.setflag(t.received_flag);
  s.say("use");
  s.release();
  s.end();
  s.label("full");
  s.say("bagfull");
  s.release();
  s.end();
  s.label("done");
  s.say("again");
  s.release();
  s.end();
  s.text("thanks", "Thanks for coming to the event,\n{PLAYER}!\fWe have something for you:\nthe " + item + ".");
  s.text("use", "Show it to the sailor at the\nLILYCOVE CITY harbor.\fWhere will it take you?");
  s.text("bagfull", "Oh, your BAG has no room for it.\fMake some room in your\nKEY ITEMS POCKET and come back.");
  s.text("again", "Thanks for coming to the event!");
  return s.finish();
}

// The Altering Cave (Route 103): Zubat only, unless the game receives a Mystery Gift script that
// moves it to one of its other eight sets of Pokémon, from Johto. Emerald still has Game Freak's
// own script for it (data/scripts/gift_altering_cave.inc: each gift moves to the next set); it
// was never distributed outside Japan. Here the player picks the set; the deliveryman's script
// sets VAR_ALTERING_CAVE_WILD_SET to it, as that script did.
struct CaveSetDef {
  const char* key;
  const char* name;   // "Mareep"
  const char* caps;   // "MAREEP"
  uint16_t species;   // the card's icon
};
const CaveSetDef kAlteringCave[] = {
    {"mareep", "Mareep", "MAREEP", 179},     {"pineco", "Pineco", "PINECO", 204},
    {"houndour", "Houndour", "HOUNDOUR", 228}, {"teddiursa", "Teddiursa", "TEDDIURSA", 216},
    {"aipom", "Aipom", "AIPOM", 190},        {"shuckle", "Shuckle", "SHUCKLE", 213},
    {"stantler", "Stantler", "STANTLER", 234}, {"smeargle", "Smeargle", "SMEARGLE", 235},
    {"zubat", "Zubat (as before)", "ZUBAT", 41},
};
constexpr uint16_t kVarAlteringCaveWildSet = 0x403E;

std::vector<uint8_t> cave_script(const CaveSetDef& c, uint16_t set) {
  const std::string mon = c.caps;
  ScriptWriter s;
  s.setvaddress();
  s.lock();
  s.faceplayer();
  s.setvar(kVarAlteringCaveWildSet, set);
  s.say("rumor");
  s.release();
  s.end();
  s.text("rumor", "Thanks for coming to the event,\n{PLAYER}!\fThey say wild " + mon + " have been\nseen in the ALTERING CAVE.\f"
                  "It's on ROUTE 103. Why not\ngo and take a look?");
  return s.finish();
}

void put_text(std::vector<uint8_t>& card, std::size_t at, const std::string& s) {
  std::vector<uint8_t> t = encode_game_text(s);
  if (t.size() > 40) {
    t.resize(40);
    t[39] = 0xFF;
  }
  for (std::size_t i = 0; i < t.size(); ++i) card[at + i] = t[i];
}

std::vector<uint8_t> ticket_card(const TicketDef& t, uint32_t id_number) {
  std::vector<uint8_t> c(332, 0);
  c[0] = static_cast<uint8_t>(t.card_flag), c[1] = static_cast<uint8_t>(t.card_flag >> 8);
  c[2] = static_cast<uint8_t>(t.icon_species), c[3] = static_cast<uint8_t>(t.icon_species >> 8);
  for (int i = 0; i < 4; ++i) c[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(id_number >> (8 * i));
  // type: gift (0); background; send type: friends may be sent it too (1).
  c[8] = static_cast<uint8_t>(0 | (t.background & 0xF) << 2 | 1 << 6);
  c[9] = 0;  // no stamps
  put_text(c, 10, t.item_caps);
  put_text(c, 50, "PROJECT PROMETHEUS EVENT");
  for (std::size_t line = 0; line < 4; ++line) put_text(c, 90 + 40 * line, t.card_lines[line]);
  put_text(c, 250, "Collect it at any");
  put_text(c, 290, "POKéMON CENTER.");
  return c;
}

std::vector<uint8_t> cave_card(const CaveSetDef& c, uint16_t card_flag, uint32_t id_number) {
  std::vector<uint8_t> card(332, 0);
  card[0] = static_cast<uint8_t>(card_flag), card[1] = static_cast<uint8_t>(card_flag >> 8);
  card[2] = static_cast<uint8_t>(c.species), card[3] = static_cast<uint8_t>(c.species >> 8);
  for (int i = 0; i < 4; ++i) card[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(id_number >> (8 * i));
  card[8] = static_cast<uint8_t>(0 | (5 & 0xF) << 2 | 1 << 6);  // gift; background 5; friends may be sent it
  put_text(card, 10, "ALTERING CAVE");
  put_text(card, 50, "PROJECT PROMETHEUS EVENT");
  put_text(card, 90, std::string("Rare POKéMON have been seen in"));
  put_text(card, 130, "the ALTERING CAVE on ROUTE 103:");
  put_text(card, 170, std::string("wild ") + c.caps + ".");
  put_text(card, 210, "");
  put_text(card, 250, "Hear more from the deliveryman");
  put_text(card, 290, "in any POKéMON CENTER.");
  return card;
}

}  // namespace

std::vector<uint8_t> encode_game_text(const std::string& text) {
  std::vector<uint8_t> out;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (text.compare(i, 8, "{PLAYER}") == 0) {
      out.push_back(0xFD), out.push_back(0x01);
      i += 7;
    } else if (text.compare(i, 2, "\xC3\xA9") == 0) {  // é
      out.push_back(0x1B);
      ++i;
    } else if (c >= 'A' && c <= 'Z') {
      out.push_back(static_cast<uint8_t>(0xBB + (c - 'A')));
    } else if (c >= 'a' && c <= 'z') {
      out.push_back(static_cast<uint8_t>(0xD5 + (c - 'a')));
    } else if (c >= '0' && c <= '9') {
      out.push_back(static_cast<uint8_t>(0xA1 + (c - '0')));
    } else {
      switch (c) {
        case ' ': out.push_back(0x00); break;
        case '!': out.push_back(0xAB); break;
        case '?': out.push_back(0xAC); break;
        case '.': out.push_back(0xAD); break;
        case '-': out.push_back(0xAE); break;
        case ',': out.push_back(0xB8); break;
        case '\'': out.push_back(0xB4); break;
        case ':': out.push_back(0xF0); break;
        case '/': out.push_back(0xBA); break;
        case '&': out.push_back(0x2D); break;
        case '\n': out.push_back(0xFE); break;
        case '\f': out.push_back(0xFB); break;
        default: break;  // not in the games' set
      }
    }
  }
  out.push_back(0xFF);
  return out;
}

uint16_t game_crc16(const uint8_t* data, std::size_t size) {
  static const std::array<uint16_t, 256> table = [] {
    std::array<uint16_t, 256> t{};
    for (unsigned i = 0; i < 256; ++i) {
      unsigned c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0x8408 : c >> 1;
      t[i] = static_cast<uint16_t>(c);
    }
    return t;
  }();
  uint16_t crc = 0x1121;
  for (std::size_t i = 0; i < size; ++i) {
    const uint8_t high = static_cast<uint8_t>(crc >> 8);
    crc = static_cast<uint16_t>(crc ^ data[i]);
    crc = static_cast<uint16_t>(high ^ table[crc & 0xFF]);
  }
  return static_cast<uint16_t>(~crc);
}

std::vector<EventGift> event_gifts(const std::string& game_code) {
  std::vector<EventGift> out;
  if (game_code == "BPEE") {
    uint32_t id = 1;
    for (const TicketDef& t : kEmerald) {
      out.push_back({t.key, t.name, t.where, "", "", t.card_flag, ticket_card(t, id++), ticket_script(t)});
    }
    // One card id per set, so choosing another set is a new card (the game offers to swap it).
    for (uint16_t i = 0; i < std::size(kAlteringCave); ++i) {
      const CaveSetDef& c = kAlteringCave[i];
      const uint16_t set = c.species == 41 ? 0 : static_cast<uint16_t>(i + 1);
      const uint16_t flag = static_cast<uint16_t>(1004 + i);
      out.push_back({std::string("altering-") + c.key, "Altering Cave", std::string("Wild ") + c.name + " in the cave on Route 103",
                     "altering", c.name, flag, cave_card(c, flag, id++), cave_script(c, set)});
    }
  }
  return out;
}

}  // namespace p64
