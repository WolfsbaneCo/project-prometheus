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
  void specialvar(uint16_t var, uint16_t special) { op(0x26), u16(var), u16(special); }
  void returnram() { op(0x0C); }
  void vcall(const std::string& dest) { op(0xBA), ref(dest); }
  void vgoto_if_unset(uint16_t flag, const std::string& dest) {
    op(0x2B), u16(flag);           // checkflag
    op(0xBB), op(0), ref(dest);    // vgoto_if FALSE (not set)
  }
  void vgoto_if_var(uint16_t var, uint16_t value, const std::string& dest) {
    op(0x21), u16(var), u16(value);  // compare_var_to_value
    op(0xBB), op(1), ref(dest);      // vgoto_if EQUAL
  }
  void giveegg(uint16_t species) { op(0x7A), u16(species); }
  void setmonmove(uint8_t party_index, uint8_t slot, uint16_t move) { op(0x7B), op(party_index), op(slot), u16(move); }
  void setmodernfatefulencounter(uint16_t slot_var) { op(0xCD), u16(slot_var); }
  void setmonmetlocation(uint16_t slot_var, uint8_t location) { op(0xD2), u16(slot_var), op(location); }
  void playfanfare(uint16_t song) { op(0x31), u16(song); }
  void checkpcitem(uint16_t item) { op(0x4A), u16(item), u16(1); }
  void killscript() { op(0x0D); }  // a RAM script: done, forget it
  void vloadptr(const std::string& text) { op(0xBE), ref(text); }  // the Mystery Event's message
  void setmysteryeventstatus(uint8_t status) { op(0x0E), op(status); }
  void waitfanfare() { op(0x32); }
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

// FireRed and LeafGreen (English): the two tickets they were made for, from the Vermilion City
// harbor's Seagallop ferry once the Sevii Islands are open. Their own flags (pokefirered).
const TicketDef kFireRedLeafGreen[] = {
    {"aurora", "Aurora Ticket", "Birth Island, where Deoxys waits", 1000, 0x173, 0x84B, 0x2A7, {0x2E4}, 410, 1,
     "AURORA TICKET",
     {"A ticket for a ship to BIRTH", "ISLAND. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
    {"mystic", "Mystic Ticket", "Navel Rock, where Lugia and Ho-Oh rest", 1001, 0x172, 0x84A, 0x2A8, {0x2F2, 0x2F3}, 249,
     2, "MYSTIC TICKET",
     {"A ticket for a ship to NAVEL", "ROCK. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
};

std::vector<uint8_t> ticket_script(const TicketDef& t, const std::string& harbor) {
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
  s.text("use", "Show it to the sailor at the\n" + harbor + " harbor.\fWhere will it take you?");
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
// Where each game keeps the cave's set (VAR_ALTERING_CAVE_WILD_SET), and where the cave is.
struct CaveGameDef {
  uint16_t var;
  const char* place;       // "ROUTE 103", as the games write it
  const char* place_name;  // "Route 103"
};
const CaveGameDef kEmeraldCave = {0x403E, "ROUTE 103", "Route 103"};
const CaveGameDef kFireRedLeafGreenCave = {0x4024, "SIX ISLAND", "Six Island"};

std::vector<uint8_t> cave_script(const CaveSetDef& c, uint16_t set, const CaveGameDef& g) {
  const std::string mon = c.caps;
  ScriptWriter s;
  s.setvaddress();
  s.lock();
  s.faceplayer();
  s.setvar(g.var, set);
  s.say("rumor");
  s.release();
  s.end();
  s.text("rumor", "Thanks for coming to the event,\n{PLAYER}!\fThey say wild " + mon + " have been\nseen in the ALTERING CAVE.\f"
                  "It's on " + std::string(g.place) + ". Why not\ngo and take a look?");
  return s.finish();
}

// The Surf Pichu egg: a Japanese Mystery Gift event whose script Emerald still has
// (data/scripts/gift_pichu.inc); it was never distributed anywhere else. The same steps: once per
// card (FLAG_MYSTERY_GIFT_DONE), a Pichu egg into the party if it has room, marked as an event
// Pokémon (fateful encounter, met location 0xFF), its third move Surf. Our own wording.
void put_text(std::vector<uint8_t>& card, std::size_t at, const std::string& s);

constexpr uint16_t kPichuCardFlag = 1013;
std::vector<uint8_t> pichu_script() {
  constexpr uint16_t kFlagMysteryGiftDone = 0x1E4, kVarSlot = 0x40DD;  // VAR_GIFT_PICHU_SLOT
  constexpr uint16_t kSpecialPartyCount = 0x86;                        // CalculatePlayerPartyCount
  ScriptWriter s;
  s.setvaddress();
  s.vgoto_if_unset(kFlagMysteryGiftDone, "give");
  s.returnram();  // already given: the deliveryman's own thank-you
  s.label("give");
  s.specialvar(kVarSlot, kSpecialPartyCount);
  s.vgoto_if_var(kVarSlot, 6, "full");
  s.setflag(kFlagMysteryGiftDone);
  s.vcall("egg");
  s.lock();
  s.faceplayer();
  s.say("gift");
  s.playfanfare(370);  // MUS_OBTAIN_ITEM
  s.waitfanfare();
  s.release();
  s.end();
  s.label("full");
  s.lock();
  s.faceplayer();
  s.say("partyfull");
  s.release();
  s.end();
  // The egg goes to the first free party slot, the party count before it: Surf as its 3rd move.
  // (Game Freak's script covers slots 2-6, as a player always had a Pokémon; slot 1 is added.)
  s.label("egg");
  s.giveegg(172);  // SPECIES_PICHU
  s.setmodernfatefulencounter(kVarSlot);
  s.setmonmetlocation(kVarSlot, 0xFF);  // METLOC_FATEFUL_ENCOUNTER
  for (uint8_t slot = 0; slot <= 5; ++slot) s.vgoto_if_var(kVarSlot, slot, "surf" + std::to_string(slot));
  s.op(0x03);  // return
  for (uint8_t slot = 0; slot <= 5; ++slot) {
    s.label("surf" + std::to_string(slot));
    s.setmonmove(slot, 2, 57);  // MOVE_SURF
    s.op(0x03);
  }
  s.text("gift", "Thanks for coming to the event,\n{PLAYER}!\fWe have a gift for you, all the\nway from JAPAN: a POKéMON EGG!\f"
                 "Please raise it with love\nand kindness.");
  s.text("partyfull", "Oh, your party is full.\fPlease come back after you've\nput a POKéMON in a PC box.");
  return s.finish();
}

std::vector<uint8_t> pichu_card(uint32_t id_number) {
  std::vector<uint8_t> card(332, 0);
  card[0] = static_cast<uint8_t>(kPichuCardFlag), card[1] = static_cast<uint8_t>(kPichuCardFlag >> 8);
  card[2] = 172, card[3] = 0;  // Pichu
  for (int i = 0; i < 4; ++i) card[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(id_number >> (8 * i));
  card[8] = static_cast<uint8_t>(0 | (6 & 0xF) << 2 | 1 << 6);  // gift; background 6; friends may be sent it
  put_text(card, 10, "POKéMON EGG");
  put_text(card, 50, "PROJECT PROMETHEUS EVENT");
  put_text(card, 90, "A POKéMON EGG from an event");
  put_text(card, 130, "held only in JAPAN. Something");
  put_text(card, 170, "special hatches from it.");
  put_text(card, 210, "");
  put_text(card, 250, "Collect it at any");
  put_text(card, 290, "POKéMON CENTER.");
  return card;
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

std::vector<uint8_t> cave_card(const CaveSetDef& c, uint16_t card_flag, uint32_t id_number, const CaveGameDef& g) {
  std::vector<uint8_t> card(332, 0);
  card[0] = static_cast<uint8_t>(card_flag), card[1] = static_cast<uint8_t>(card_flag >> 8);
  card[2] = static_cast<uint8_t>(c.species), card[3] = static_cast<uint8_t>(c.species >> 8);
  for (int i = 0; i < 4; ++i) card[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(id_number >> (8 * i));
  card[8] = static_cast<uint8_t>(0 | (5 & 0xF) << 2 | 1 << 6);  // gift; background 5; friends may be sent it
  put_text(card, 10, "ALTERING CAVE");
  put_text(card, 50, "PROJECT PROMETHEUS EVENT");
  put_text(card, 90, std::string("Rare POKéMON have been seen in"));
  put_text(card, 130, std::string("the ALTERING CAVE on ") + g.place + ":");
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

// Ruby and Sapphire: the Eon Ticket came from an e-Reader card, read into the game's Mystery Event
// menu over the link cable. The event data is a Mystery Event script (pokeruby,
// mystery_event_script.c), laid out as Game Freak's own test event for it
// (data/debug_mystery_event_scripts.s): a compatibility check, a CRC over the rest, a script put
// on DAD in the Petalburg City Gym (who hands the ticket over), the ticket as a record mixing
// gift, and a check run at once that sets the message the game shows. Our own wording.
std::vector<uint8_t> eon_ticket_event() {
  constexpr uint16_t kEonTicket = 275, kFlagHasEonTicket = 0x853, kFlagMetLati = 0xCE;
  // DAD's script, kept in the save until it has handed the ticket over.
  ScriptWriter dad;
  dad.setvaddress();
  dad.checkitem(kEonTicket);
  dad.vgoto_if_result(1, "done");
  dad.checkpcitem(kEonTicket);
  dad.vgoto_if_result(1, "done");
  dad.vgoto_if_set(kFlagMetLati, "done");
  dad.lock();
  dad.faceplayer();
  dad.say("letter");
  dad.checkitemspace(kEonTicket);
  dad.vgoto_if_result(0, "full");
  dad.giveitem(kEonTicket);
  dad.setflag(kFlagHasEonTicket);
  dad.say("ticket");
  dad.release();
  dad.label("done");
  dad.killscript();
  dad.label("full");
  dad.say("bagfull");
  dad.release();
  dad.end();
  dad.text("letter", "DAD: {PLAYER}! Good to see you!\nA letter came for you, {PLAYER}.");
  dad.text("ticket", "DAD: It looks like a ferry TICKET,\nbut not one I've ever seen.\f"
                     "Why not ask about it at the\nLILYCOVE CITY harbor?");
  dad.text("bagfull", "DAD: {PLAYER}, the KEY ITEMS POCKET\nof your BAG is full.\f"
                      "Put some key items in your PC,\nthen come and see me.");
  const std::vector<uint8_t> dad_script = dad.finish();

  // Run as the event loads: the message, and whether it may load (once per game).
  ScriptWriter now;
  now.setvaddress();
  now.checkitem(kEonTicket);
  now.vgoto_if_result(1, "once");
  now.checkpcitem(kEonTicket);
  now.vgoto_if_result(1, "once");
  now.vgoto_if_set(kFlagMetLati, "once");
  now.checkitemspace(kEonTicket);
  now.vgoto_if_result(0, "full");
  now.vloadptr("ok");
  now.setmysteryeventstatus(2);  // MEVENT_STATUS_SUCCESS: the game saves
  now.end();
  now.label("once");
  now.vloadptr("again");
  now.setmysteryeventstatus(3);
  now.end();
  now.label("full");
  now.vloadptr("bagfull");
  now.setmysteryeventstatus(3);
  now.end();
  now.text("ok", "Go and see your DAD at the GYM\nin PETALBURG CITY.");
  now.text("again", "This EVENT can be loaded only once.");
  now.text("bagfull", "Your BAG's KEY ITEMS POCKET is full.");
  const std::vector<uint8_t> now_script = now.finish();

  // The event script. Its pointers assume it sits at kBase (checkcompat says so).
  constexpr uint32_t kBase = 0x02000000;
  std::vector<uint8_t> e;
  auto u8 = [&](uint32_t v) { e.push_back(static_cast<uint8_t>(v)); };
  auto u16 = [&](uint32_t v) { u8(v & 0xFF), u8(v >> 8); };
  auto u32 = [&](uint32_t v) { u16(v & 0xFFFF), u16(v >> 16); };
  auto put32 = [&](std::size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) e[at + static_cast<std::size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
  };
  u8(1), u32(kBase), u16(1 << 1), u32(1 << 1), u16(0x4), u32((1 << 7) | (1 << 8));  // checkcompat: English, Ruby, Sapphire
  const std::size_t crc_at = e.size();
  u8(16), u32(0), u32(0), u32(0);  // crc, from, to: filled in below
  const std::size_t body = e.size();
  u8(6), u8(8), u8(1), u8(1);      // initramscript MAP_PETALBURG_CITY_GYM (8, 1), DAD (object 1)
  const std::size_t dad_ptrs = e.size();
  u32(0), u32(0);
  u8(11), u8(1), u8(5), u16(kEonTicket);  // setrecordmixinggift: friends can get it by mixing records
  u8(5);                                  // runscript
  const std::size_t now_ptr = e.size();
  u32(0);
  u8(2);  // end
  const std::size_t dad_at = e.size();
  e.insert(e.end(), dad_script.begin(), dad_script.end());
  const std::size_t now_at = e.size();
  e.insert(e.end(), now_script.begin(), now_script.end());
  put32(dad_ptrs, kBase + static_cast<uint32_t>(dad_at));
  put32(dad_ptrs + 4, kBase + static_cast<uint32_t>(now_at));
  put32(now_ptr, kBase + static_cast<uint32_t>(now_at));
  put32(crc_at + 5, kBase + static_cast<uint32_t>(body));
  put32(crc_at + 9, kBase + static_cast<uint32_t>(e.size()));
  put32(crc_at + 1, game_crc16(e.data() + body, e.size() - body));
  return e;
}

std::vector<EventGift> event_gifts(const std::string& game_code) {
  std::vector<EventGift> out;
  if (game_code == "AXVE" || game_code == "AXPE") {
    EventGift eon{"eon", "Eon Ticket", "Southern Island, where Latias or Latios lives", "", "", 0, {}, eon_ticket_event()};
    eon.cable = true;
    out.push_back(std::move(eon));
    return out;
  }
  const bool emerald = game_code == "BPEE";
  const bool frlg = game_code == "BPRE" || game_code == "BPGE";
  if (!emerald && !frlg) return out;
  uint32_t id = 1;
  const std::string harbor = emerald ? "LILYCOVE CITY" : "VERMILION CITY";
  for (const TicketDef& t : emerald ? std::vector<TicketDef>(std::begin(kEmerald), std::end(kEmerald))
                                    : std::vector<TicketDef>(std::begin(kFireRedLeafGreen), std::end(kFireRedLeafGreen))) {
    out.push_back({t.key, t.name, t.where, "", "", t.card_flag, ticket_card(t, id++), ticket_script(t, harbor)});
  }
  if (emerald) {
    out.push_back({"pichu", "Pichu Egg", "A Pichu that knows Surf, from an event held only in Japan", "", "",
                   kPichuCardFlag, pichu_card(id++), pichu_script()});
  }
  // One card id per set, so choosing another set is a new card (the game offers to swap it).
  const CaveGameDef& cave = emerald ? kEmeraldCave : kFireRedLeafGreenCave;
  for (uint16_t i = 0; i < std::size(kAlteringCave); ++i) {
    const CaveSetDef& c = kAlteringCave[i];
    const uint16_t set = c.species == 41 ? 0 : static_cast<uint16_t>(i + 1);
    const uint16_t flag = static_cast<uint16_t>(1004 + i);
    out.push_back({std::string("altering-") + c.key, "Altering Cave",
                   std::string("Wild ") + c.name + " in the cave on " + cave.place_name, "altering", c.name, flag,
                   cave_card(c, flag, id++, cave), cave_script(c, set, cave)});
  }
  return out;
}

}  // namespace p64
