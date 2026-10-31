#include "gift_catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
    {"aurora", "Aurora Ticket", "Ferry to Birth Island, where Deoxys waits. From the deliveryman; sail from Lilycove City after the Hall of Fame", 1000, 0x173, 0x8D5, 0x13A, {0x1AD}, 410, 1,
     "AURORA TICKET",
     {"A ticket for a ship to BIRTH", "ISLAND, far to the south. Pick", "it up from the deliveryman in any",
      "POKéMON CENTER."}},
    {"mystic", "Mystic Ticket", "Ferry to Navel Rock (Lugia and Ho-Oh). From the deliveryman; sail from Lilycove City after the Hall of Fame", 1001, 0x172, 0x8E0, 0x13B, {0x91, 0x92}, 249,
     2, "MYSTIC TICKET",
     {"A ticket for a ship to NAVEL", "ROCK. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
    {"oldseamap", "Old Sea Map", "Ferry to Faraway Island, where Mew hides. From the deliveryman; sail from Lilycove City after the Hall of Fame", 1002, 0x178, 0x8D6, 0x13C, {0x1CA}, 151, 3,
     "OLD SEA MAP",
     {"An old map of an island no one", "has charted. Pick it up from the", "deliveryman in any POKéMON",
      "CENTER."}},
    {"eon", "Eon Ticket", "Ferry to Southern Island (Latias or Latios). From the deliveryman; sail from Lilycove City after the Hall of Fame", 1003, 0x113, 0x8B3, 0x13D, {0x1C9}, 407, 4,
     "EON TICKET",
     {"A ticket for a ship to SOUTHERN", "ISLAND. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
};

// FireRed and LeafGreen (English): the two tickets they were made for, from the Vermilion City
// harbor's Seagallop ferry once the Sevii Islands are open. Their own flags (pokefirered).
const TicketDef kFireRedLeafGreen[] = {
    {"aurora", "Aurora Ticket", "Seagallop ferry to Birth Island (Deoxys), from Vermilion City once the Sevii Islands are open", 1000, 0x173, 0x84B, 0x2A7, {0x2E4}, 410, 1,
     "AURORA TICKET",
     {"A ticket for a ship to BIRTH", "ISLAND. Pick it up from the", "deliveryman in any POKéMON", "CENTER."}},
    {"mystic", "Mystic Ticket", "Seagallop ferry to Navel Rock (Lugia, Ho-Oh), from Vermilion City once the Sevii Islands are open", 1001, 0x172, 0x84A, 0x2A8, {0x2F2, 0x2F3}, 249,
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

// A Ruby and Sapphire Mystery Event (pokeruby, mystery_event_script.c): the commands the game runs
// once the event is in, then the data they point to. Laid out as Game Freak's own test event
// (data/debug_mystery_event_scripts.s): a compatibility check, a CRC over everything after it, the
// commands, end. The game shows the message the last command left and saves when it says so.
class MysteryEventWriter {
 public:
  static constexpr uint32_t kBase = 0x02000000;  // where the pointers assume the event sits

  MysteryEventWriter() {
    u8(1), u32(kBase), u16(1 << 1), u32(1 << 1), u16(0x4), u32((1 << 7) | (1 << 8));  // checkcompat: English, Ruby, Sapphire
    crc_at_ = e_.size();
    u8(16), u32(0), u32(0), u32(0);  // crc, from, to: filled in by finish()
    body_ = e_.size();
  }
  void initramscript(uint8_t map_group, uint8_t map_num, uint8_t object, std::vector<uint8_t> script) {
    u8(6), u8(map_group), u8(map_num), u8(object);
    const std::size_t b = blob(std::move(script));
    ref(b, false), ref(b, true);
  }
  void runscript(std::vector<uint8_t> script) { u8(5), ref(blob(std::move(script)), false); }
  void setenigmaberry(std::vector<uint8_t> berry) { u8(7), ref(blob(std::move(berry)), false); }
  void addrareword(uint8_t word) { u8(10), u8(word); }
  void setrecordmixinggift(uint8_t count, uint16_t item) { u8(11), u8(1), u8(count), u16(item); }
  // A struct Pokemon (100 bytes) and the mail it holds, if any (struct MailStruct, 36 bytes).
  void givepokemon(std::vector<uint8_t> mon_and_mail) { u8(12), ref(blob(std::move(mon_and_mail)), false); }
  void addtrainer(std::vector<uint8_t> trainer) { u8(13), ref(blob(std::move(trainer)), false); }
  void enableresetrtc() { u8(14); }

  std::vector<uint8_t> finish() {
    u8(2);  // end
    std::vector<std::size_t> at;
    for (const std::vector<uint8_t>& b : blobs_) {
      while (e_.size() % 4) e_.push_back(0);  // the game copies some of these as words
      at.push_back(e_.size());
      e_.insert(e_.end(), b.begin(), b.end());
    }
    // The game reads the event from its shared buffer, where only blocks over 256 bytes land
    // (link.c: smaller ones go to the link's own buffer), as e-Reader cards always were.
    while (e_.size() <= 0x100) e_.push_back(0);
    for (const Fixup& f : fixups_) put32(f.offset, kBase + static_cast<uint32_t>(at[f.blob] + (f.end ? blobs_[f.blob].size() : 0)));
    put32(crc_at_ + 5, kBase + static_cast<uint32_t>(body_));
    put32(crc_at_ + 9, kBase + static_cast<uint32_t>(e_.size()));
    put32(crc_at_ + 1, game_crc16(e_.data() + body_, e_.size() - body_));
    return e_;
  }

 private:
  struct Fixup {
    std::size_t offset, blob;
    bool end;
  };
  void u8(uint32_t v) { e_.push_back(static_cast<uint8_t>(v)); }
  void u16(uint32_t v) { u8(v & 0xFF), u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xFFFF), u16(v >> 16); }
  void put32(std::size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) e_[at + static_cast<std::size_t>(i)] = static_cast<uint8_t>(v >> (8 * i));
  }
  std::size_t blob(std::vector<uint8_t> b) {
    blobs_.push_back(std::move(b));
    return blobs_.size() - 1;
  }
  void ref(std::size_t b, bool end) {
    fixups_.push_back({e_.size(), b, end});
    u32(0);
  }

  std::vector<uint8_t> e_;
  std::size_t crc_at_ = 0, body_ = 0;
  std::vector<std::vector<uint8_t>> blobs_;
  std::vector<Fixup> fixups_;
};

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

  MysteryEventWriter e;
  e.initramscript(8, 1, 1, dad_script);  // MAP_PETALBURG_CITY_GYM (8, 1), DAD (object 1)
  e.setrecordmixinggift(5, kEonTicket);  // friends can get it by mixing records
  e.runscript(now_script);
  return e.finish();
}

// A script the event runs at once, for the message the game shows when it has loaded (and saved).
std::vector<uint8_t> message_script(const std::string& text) {
  ScriptWriter s;
  s.setvaddress();
  s.vloadptr("text");
  s.setmysteryeventstatus(2);  // MEVENT_STATUS_SUCCESS
  s.end();
  s.text("text", text);
  return s.finish();
}

// Ruby and Sapphire's clock fix: what Nintendo sent out when the games' berries stopped growing (the
// clock's day count goes wrong after a year). The game's own command turns on its clock-reset
// screen: at the title screen, hold B, SELECT and Left.
std::vector<uint8_t> clock_fix_event() {
  MysteryEventWriter e;
  e.enableresetrtc();
  e.runscript(message_script("To set the clock, hold B, SELECT\nand Left on the title screen."));
  return e.finish();
}

// The rare words: 33 easy chat words ("trendy sayings") that came one at a time from e-Reader cards
// and from the man in Mauville City. All of them, in one event.
std::vector<uint8_t> rare_words_event() {
  MysteryEventWriter e;
  for (uint8_t i = 0; i < 33; ++i) e.addrareword(i);
  e.runscript(message_script("All 33 rare words are now in\nyour EASY CHAT words."));
  return e.finish();
}

// The Enigma Berry. The game keeps one berry of its own design in the save (struct EnigmaBerry,
// pokeruby include/global.berry.h): its name, how it grows and tastes, a 48x48 picture, its
// description, and what it does. e-Reader cards sent such a berry; DAD in the Petalburg City Gym
// then hands one over (once his badge is won). Here: the game's own name, growth, flavours and
// description for it; our own picture; held, it restores 30 HP when HP falls below half (as a
// Sitrus Berry does in these games), and eaten from the bag it restores 30 HP.
std::vector<uint8_t> enigma_berry_pic(std::array<uint16_t, 16>& palette) {
  auto rgb = [](int r, int g, int b) { return static_cast<uint16_t>(r | g << 5 | b << 10); };
  palette = {0,                rgb(5, 3, 10),   rgb(10, 6, 18),  rgb(15, 9, 25),  rgb(21, 15, 29), rgb(28, 25, 31),
             rgb(31, 26, 6),   rgb(31, 31, 20), rgb(4, 12, 5),   rgb(9, 21, 8),   rgb(16, 27, 11), rgb(13, 8, 3),
             0, 0, 0, 0};
  constexpr int kSize = 48;
  std::array<uint8_t, kSize * kSize> px{};
  const double pi = 3.14159265358979;
  // The berry: round with five soft lobes, lit from the top left.
  auto inside_body = [&](double x, double y) {
    const double dx = (x - 24.0) / 15.5, dy = (y - 28.0) / 14.5;
    const double a = std::atan2(dy, dx);
    return std::sqrt(dx * dx + dy * dy) < 1.0 + 0.07 * std::cos(5 * (a + pi / 2));
  };
  // Two leaves and a stem on top.
  auto in_leaf = [&](double x, double y, double cx, double cy, double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    const double u = ((x - cx) * c + (y - cy) * s) / 7.0, v = (-(x - cx) * s + (y - cy) * c) / 3.2;
    return u * u + v * v < 1.0 ? (v < 0 ? 2 : 1) : 0;
  };
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const double fx = x + 0.5, fy = y + 0.5;
      uint8_t c = 0;
      const int l1 = in_leaf(fx, fy, 18.0, 12.0, -0.45), l2 = in_leaf(fx, fy, 30.0, 12.0, 0.45);
      if (l1 || l2) c = (l1 == 2 || l2 == 2) ? 10 : 9;
      if (fx >= 23 && fx <= 25.5 && fy >= 8 && fy <= 16) c = 11;
      if (inside_body(fx, fy)) {
        const double nx = (fx - 24.0) / 15.5, ny = (fy - 28.0) / 14.5;
        const double nz = std::sqrt(std::max(0.0, 1.0 - nx * nx - ny * ny));
        const double light = -0.45 * nx - 0.55 * ny + 0.70 * nz;
        c = light > 0.93 ? 5 : light > 0.62 ? 4 : light > 0.25 ? 3 : 2;
        // A five-pointed star on its face.
        const double sx = fx - 25.0, sy = fy - 30.0;
        const double r = std::sqrt(sx * sx + sy * sy), a = std::atan2(sy, sx) + pi / 2;
        const double phi = std::fabs(std::fmod(a + 2 * pi * 4, 2 * pi / 5) - pi / 5);  // 0..36 degrees from a point
        // The edge from a point (6.5 out) to the notch beside it (2.7 out), where this angle crosses it.
        const double px = 6.5, py = 0.0, qx = 2.7 * std::cos(pi / 5), qy = 2.7 * std::sin(pi / 5);
        const double dx2 = qx - px, dy2 = qy - py;
        const double edge = (px * dy2 - py * dx2) / (std::cos(phi) * dy2 - std::sin(phi) * dx2);
        if (r < edge) c = r < 1.6 ? 7 : 6;
      }
      px[static_cast<std::size_t>(y * kSize + x)] = c;
    }
  }
  // Outlines: the body's and the leaves' edges against the empty background.
  std::array<uint8_t, kSize * kSize> out = px;
  for (int y = 0; y < kSize; ++y) {
    for (int x = 0; x < kSize; ++x) {
      const uint8_t c = px[static_cast<std::size_t>(y * kSize + x)];
      if (!c) continue;
      bool edge = false;
      for (const auto& [ox, oy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
        const int nx = x + ox, ny = y + oy;
        if (nx < 0 || ny < 0 || nx >= kSize || ny >= kSize || !px[static_cast<std::size_t>(ny * kSize + nx)]) edge = true;
      }
      if (edge) out[static_cast<std::size_t>(y * kSize + x)] = c >= 8 && c <= 11 ? 8 : 1;
    }
  }
  // 4 bits a pixel, in 8x8 tiles, six rows of six.
  std::vector<uint8_t> pic;
  for (int ty = 0; ty < 6; ++ty) {
    for (int tx = 0; tx < 6; ++tx) {
      for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; x += 2) {
          const std::size_t at = static_cast<std::size_t>((ty * 8 + y) * kSize + tx * 8 + x);
          pic.push_back(static_cast<uint8_t>(out[at] | out[at + 1] << 4));
        }
      }
    }
  }
  return pic;
}

std::vector<uint8_t> enigma_berry() {
  std::vector<uint8_t> b(0x530, 0);
  auto put = [&](std::size_t at, const std::string& text, std::size_t size) {
    const std::vector<uint8_t> t = encode_game_text(text);
    for (std::size_t i = 0; i < size; ++i) b[at + i] = i < t.size() ? t[i] : 0xFF;
  };
  put(0x00, "ENIGMA", 7);
  b[0x07] = 3;                       // firmness: hard
  b[0x08] = 155, b[0x09] = 0;        // size: 155 mm
  b[0x0A] = 2, b[0x0B] = 1;          // yield per tree: 1 to 2 (the game's own Enigma entry)
  b[0x14] = 24;                      // hours a growth stage, as the game's own entry
  for (std::size_t i = 0x15; i <= 0x1A; ++i) b[i] = 40;  // spicy, dry, sweet, bitter, sour; smoothness
  std::array<uint16_t, 16> palette{};
  const std::vector<uint8_t> pic = enigma_berry_pic(palette);
  std::copy(pic.begin(), pic.end(), b.begin() + 0x1C);
  for (std::size_t i = 0; i < 16; ++i) b[0x49C + 2 * i] = static_cast<uint8_t>(palette[i]), b[0x49D + 2 * i] = static_cast<uint8_t>(palette[i] >> 8);
  put(0x4BC, "A completely enigmatic BERRY.", 45);
  put(0x4E9, "Appears to have the power of stars.", 45);
  const uint8_t effect[] = {0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 30};  // eaten: restores 30 HP (as Sitrus)
  std::copy(std::begin(effect), std::end(effect), b.begin() + 0x516);
  b[0x528] = 1, b[0x529] = 30;       // held: HOLD_EFFECT_RESTORE_HP, 30 HP
  uint32_t sum = 0;
  for (std::size_t i = 0; i < 0x52C; ++i) sum += b[i];
  for (int i = 0; i < 4; ++i) b[0x52C + static_cast<std::size_t>(i)] = static_cast<uint8_t>(sum >> (8 * i));
  return b;
}

std::vector<uint8_t> enigma_berry_event() {
  MysteryEventWriter e;
  e.setenigmaberry(enigma_berry());
  return e.finish();
}

// Battle e-Cards: an e-Reader card could put one extra trainer in the Battle Tower (struct
// BattleTowerEReaderTrainer, pokeruby include/global.h). The game sends that trainer in when the
// win streak matches the card's and the level type matches the team's (all Lv. 50, or all
// Lv. 100), once; the next card replaces it. Ours: our own trainers and teams, met as the first
// battle of a new challenge (a streak of 0). Easy chat words and ids from the game's tables.
namespace ec {
constexpr uint16_t w(uint16_t group, uint16_t i) { return static_cast<uint16_t>(group << 9 | i); }
constexpr uint16_t kNone = 0xFFFF;
constexpr uint16_t TRAINER_ = w(0x1, 0xb), GO = w(0x3, 0x1), WIN = w(0x3, 0x6), WON = w(0x3, 0x7), POWER = w(0x3, 0x1e),
                   STRONG = w(0x3, 0x20), BATTLE = w(0x3, 0x28), THANK_YOU = w(0x4, 0x11), HELLO = w(0x4, 0xf),
                   I = w(0x5, 0x1), YOU = w(0x5, 0x2), YOUR = w(0x5, 0x5), ME = w(0x5, 0x14), MY = w(0x5, 0x28),
                   EXCL = w(0x6, 0x0), WOW = w(0x6, 0x12), WILL = w(0x8, 0x0), LET_S = w(0x8, 0xd), ARE = w(0x8, 0xf),
                   ON = w(0x8, 0x2c), TO = w(0x8, 0x2f), MEET = w(0x9, 0x0), NICE = w(0x9, 0x32), SEE = w(0x9, 0x39),
                   GREAT = w(0x9, 0x40), COME = w(0xb, 0x1b), SHOW = w(0xb, 0x42), TIME = w(0xe, 0x13),
                   NEXT = w(0xe, 0x1c), AWESOME = w(0x10, 0x17);
}  // namespace ec

struct ECardMon {
  uint16_t species;
  const char* name;
  uint16_t item;
  uint16_t moves[4];
  uint8_t nature;      // personality % 25
  uint8_t evs[6];      // HP, Attack, Defense, Speed, Sp. Atk, Sp. Def
  bool alt_ability;
};
struct ECardTrainer {
  const char* key;
  const char* name;
  uint8_t trainer_class;  // Ruby and Sapphire's Battle Tower classes (gTrainerClassToPicIndex)
  uint8_t emerald_class;  // Emerald's facility classes (FACILITY_CLASS_*)
  uint16_t trainer_id;
  uint16_t greeting[6], player_lost[6], player_won[6];
  ECardMon party[3];
};
const ECardTrainer kECardTrainers[] = {
    {"mira", "MIRA", 9, 0x12, 31402,  // Cool Trainer (F)
     {ec::LET_S, ec::BATTLE, ec::EXCL, ec::SHOW, ec::YOUR, ec::POWER},
     {ec::I, ec::WON, ec::EXCL, ec::NICE, ec::BATTLE, ec::EXCL},
     {ec::YOU, ec::ARE, ec::STRONG, ec::GREAT, ec::BATTLE, ec::EXCL},
     {{121, "STARMIE", 200, {57, 85, 58, 105}, 10, {6, 0, 0, 252, 252, 0}, true},           // Leftovers; Surf, Thunderbolt, Ice Beam, Recover
      {143, "SNORLAX", 134, {34, 89, 156, 247}, 3, {252, 252, 0, 0, 0, 6}, true},           // Chesto; Body Slam, Earthquake, Rest, Shadow Ball
      {394, "GARDEVOIR", 141, {94, 347, 85, 247}, 15, {6, 0, 0, 252, 252, 0}, false}}},     // Lum; Psychic, Calm Mind, Thunderbolt, Shadow Ball
    {"dorian", "DORIAN", 34, 0x09, 50817,  // Expert (M)
     {ec::I, ec::WILL, ec::WIN, ec::COME, ec::ON, ec::EXCL},
     {ec::MY, ec::POWER, ec::WON, ec::NEXT, ec::TIME, ec::EXCL},
     {ec::WOW, ec::EXCL, ec::AWESOME, ec::YOU, ec::WON, ec::EXCL},
     {{68, "MACHAMP", 186, {238, 157, 89, 339}, 3, {252, 252, 0, 6, 0, 0}, false},          // Choice Band; Cross Chop, Rock Slide, Earthquake, Bulk Up
      {400, "METAGROSS", 200, {309, 89, 94, 182}, 3, {252, 252, 0, 6, 0, 0}, false},        // Leftovers; Meteor Mash, Earthquake, Psychic, Protect
      {397, "SALAMENCE", 170, {337, 53, 332, 349}, 19, {0, 252, 0, 252, 6, 0}, false}}},    // Salac; Dragon Claw, Flamethrower, Aerial Ace, Dragon Dance
    {"selene", "SELENE", 67, 0x45, 7746,  // Pokémon Ranger (F)
     {ec::NICE, ec::TO, ec::MEET, ec::YOU, ec::LET_S, ec::BATTLE},
     {ec::I, ec::WON, ec::EXCL, ec::SEE, ec::YOU, ec::EXCL},
     {ec::THANK_YOU, ec::EXCL, ec::kNone, ec::GREAT, ec::BATTLE, ec::EXCL},
     {{329, "MILOTIC", 200, {57, 58, 105, 92}, 5, {252, 0, 252, 0, 6, 0}, false},           // Leftovers; Surf, Ice Beam, Recover, Toxic
      {307, "BRELOOM", 168, {147, 183, 327, 202}, 3, {6, 252, 0, 252, 0, 0}, false},        // Liechi; Spore, Mach Punch, Sky Uppercut, Giga Drain
      {227, "SKARMORY", 219, {65, 191, 92, 182}, 8, {252, 0, 252, 0, 0, 6}, true}}},        // Shell Bell; Drill Peck, Spikes, Toxic, Protect
};

std::vector<uint8_t> ecard_trainer(const ECardTrainer& t, uint8_t level, uint8_t trainer_class) {
  std::vector<uint8_t> b(0xBC, 0);
  auto put16 = [&](std::size_t at, uint16_t v) { b[at] = static_cast<uint8_t>(v), b[at + 1] = static_cast<uint8_t>(v >> 8); };
  auto put32 = [&](std::size_t at, uint32_t v) { put16(at, static_cast<uint16_t>(v)), put16(at + 2, static_cast<uint16_t>(v >> 16)); };
  auto put_text = [&](std::size_t at, const std::string& text, std::size_t size) {
    const std::vector<uint8_t> s = encode_game_text(text);
    for (std::size_t i = 0; i < size; ++i) b[at + i] = i < s.size() ? s[i] : 0xFF;
  };
  const uint32_t ot_id = static_cast<uint32_t>(t.trainer_id) | static_cast<uint32_t>(t.trainer_id ^ 0x5A5A) << 16;
  b[0x01] = trainer_class;
  put16(0x02, 0);  // win streak: the first battle of a challenge
  put_text(0x04, t.name, 8);
  put32(0x0C, ot_id);
  for (std::size_t i = 0; i < 6; ++i) {
    put16(0x10 + 2 * i, t.greeting[i]);
    put16(0x1C + 2 * i, t.player_lost[i]);
    put16(0x28 + 2 * i, t.player_won[i]);
  }
  for (std::size_t m = 0; m < 3; ++m) {
    const ECardMon& mon = t.party[m];
    const std::size_t at = 0x34 + 44 * m;
    put16(at, mon.species);
    put16(at + 2, mon.item);
    for (std::size_t i = 0; i < 4; ++i) put16(at + 4 + 2 * i, mon.moves[i]);
    b[at + 0x0C] = level;
    for (std::size_t i = 0; i < 6; ++i) b[at + 0x0E + i] = mon.evs[i];
    put32(at + 0x14, ot_id);
    put32(at + 0x18, 0x3FFFFFFFu | (mon.alt_ability ? 0x80000000u : 0));  // every IV 31
    // A personality with the nature asked for, and not shiny.
    uint32_t p = 0x4E1C0000u + static_cast<uint32_t>(m) * 0x1357u + t.trainer_id;
    auto shiny = [&](uint32_t v) { return ((ot_id >> 16) ^ (ot_id & 0xFFFF) ^ (v >> 16) ^ (v & 0xFFFF)) < 8; };
    while (p % 25 != mon.nature || shiny(p)) ++p;
    put32(at + 0x1C, p);
    put_text(at + 0x20, mon.name, 11);
    b[at + 0x2B] = 255;  // friendship
  }
  uint32_t sum = 0;
  for (std::size_t i = 0; i < 0xB8; i += 4) sum += static_cast<uint32_t>(b[i] | b[i + 1] << 8 | b[i + 2] << 16 | b[i + 3] << 24);
  put32(0xB8, sum);
  return b;
}

std::vector<uint8_t> ecard_trainer_event(const ECardTrainer& t, uint8_t level) {
  MysteryEventWriter e;
  e.addtrainer(ecard_trainer(t, level, t.trainer_class));
  e.runscript(message_script(std::string("A new TRAINER waits at the BATTLE\nTOWER: LV. ") + (level == 50 ? "50" : "100") +
                             ", first battle."));
  return e.finish();
}

// Jirachi, as the Pokémon Colosseum Bonus Disc (North America, 2004) sent it to Ruby and Sapphire:
// OT WISHMKR, ID 20043, Lv. 5, Wish, Confusion and Rest, a Salac or Ganlon Berry, a Poké Ball, met
// in a "fateful" place (met location 255), from Ruby. The disc drew everything else from one 16-bit
// seed, the checksum of the first section of the game's save, with the games' own random numbers
// (the LCRNG): the personality from the next two numbers (high half first), the IVs from the two
// after, the berry from the fifth. So one save always got the same Jirachi, as here. (Generation
// as documented by PKHeX's legality checker, Legality/RNG/ClassicEra/Gen3/Wishmkr.cs and
// EncounterGift3.cs.) The game's own Mystery Event command puts it in the party
// (MEScrCmd_givepokemon), marks the Pokédex and shows "JIRACHI was sent over!"
std::vector<uint8_t> wishmkr_jirachi(uint16_t seed16) {
  uint32_t seed = seed16;
  auto rand16 = [&] {
    seed = seed * 0x41C64E6Du + 0x6073u;
    return static_cast<uint16_t>(seed >> 16);
  };
  const uint16_t pid_hi = rand16(), pid_lo = rand16();
  const uint32_t pid = static_cast<uint32_t>(pid_hi) << 16 | pid_lo;
  const uint16_t iv1 = rand16(), iv2 = rand16();
  const uint16_t item = static_cast<uint16_t>(170 - ((rand16() / 3) & 1));  // Salac (170) or Ganlon (169)
  const uint32_t ot_id = 20043;  // secret ID 0
  const int ivs[6] = {iv1 & 31, iv1 >> 5 & 31, iv1 >> 10 & 31, iv2 & 31, iv2 >> 5 & 31, iv2 >> 10 & 31};  // HP Atk Def Spe SpA SpD
  constexpr uint8_t kLevel = 5;

  std::vector<uint8_t> m(100 + 36, 0);  // struct Pokemon, then the mail it would hold (none)
  auto put16 = [](uint8_t* p, uint32_t v) { p[0] = static_cast<uint8_t>(v), p[1] = static_cast<uint8_t>(v >> 8); };
  auto put32 = [&](uint8_t* p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); };
  put32(&m[0], pid);
  put32(&m[4], ot_id);
  const std::vector<uint8_t> nick = encode_game_text("JIRACHI"), ot = encode_game_text("WISHMKR");
  for (std::size_t i = 0; i < 10; ++i) m[8 + i] = i < nick.size() ? nick[i] : 0xFF;
  m[18] = 2;     // English
  m[19] = 0x02;  // has a species
  for (std::size_t i = 0; i < 7; ++i) m[20 + i] = ot[i];  // seven letters, no terminator
  // The four substructures, in the order the personality picks.
  uint8_t g[12] = {}, a[12] = {}, e[12] = {}, mi[12] = {};
  put16(g, 409);          // SPECIES_JIRACHI
  put16(g + 2, item);
  put32(g + 4, 156);      // experience at Lv. 5 (slow growth: 5 * 5^3 / 4)
  g[9] = 100;             // friendship: Jirachi's base
  put16(a, 273), put16(a + 2, 93), put16(a + 4, 156);  // Wish, Confusion, Rest
  a[8] = 10, a[9] = 25, a[10] = 10;                    // their PP
  mi[1] = 255;            // met location: a fateful encounter
  put16(mi + 2, kLevel | 2u << 7 | 4u << 11);          // met at Lv. 5, in Ruby, in a Poké Ball; OT male
  uint32_t iv_word = 0;
  for (int i = 0; i < 6; ++i) iv_word |= static_cast<uint32_t>(ivs[i]) << (5 * i);
  put32(mi + 4, iv_word);
  static const char* const kOrders[24] = {"GAEM", "GAME", "GEAM", "GEMA", "GMAE", "GMEA", "AGEM", "AGME",
                                          "AEGM", "AEMG", "AMGE", "AMEG", "EGAM", "EGMA", "EAGM", "EAMG",
                                          "EMGA", "EMAG", "MGAE", "MGEA", "MAGE", "MAEG", "MEGA", "MEAG"};
  uint8_t plain[48];
  for (int i = 0; i < 4; ++i) {
    const char c = kOrders[pid % 24][i];
    std::copy_n(c == 'G' ? g : c == 'A' ? a : c == 'E' ? e : mi, 12, plain + 12 * i);
  }
  uint16_t checksum = 0;
  for (int i = 0; i < 48; i += 2) checksum = static_cast<uint16_t>(checksum + (plain[i] | plain[i + 1] << 8));
  put16(&m[28], checksum);
  const uint32_t key = pid ^ ot_id;
  for (int i = 0; i < 48; i += 4) {
    const uint32_t w = static_cast<uint32_t>(plain[i] | plain[i + 1] << 8 | plain[i + 2] << 16) | static_cast<uint32_t>(plain[i + 3]) << 24;
    put32(&m[32 + static_cast<std::size_t>(i)], w ^ key);
  }
  // The party part: level and stats, which the game takes as they are.
  const int nature = static_cast<int>(pid % 25), up = nature / 5, down = nature % 5;  // over Atk Def Spe SpA SpD
  m[84] = kLevel;
  m[85] = 0xFF;  // no mail
  const int hp = (200 + ivs[0]) * kLevel / 100 + kLevel + 10;
  put16(&m[86], static_cast<uint32_t>(hp)), put16(&m[88], static_cast<uint32_t>(hp));
  for (int s = 0; s < 5; ++s) {
    int v = (200 + ivs[s + 1]) * kLevel / 100 + 5;
    if (up != down && s == up) v = v * 110 / 100;
    if (up != down && s == down) v = v * 90 / 100;
    put16(&m[90 + 2 * static_cast<std::size_t>(s)], static_cast<uint32_t>(v));
  }
  return m;
}

std::vector<uint8_t> jirachi_event(uint16_t seed) {
  MysteryEventWriter e;
  e.givepokemon(wishmkr_jirachi(seed));
  return e.finish();
}

RubySapphireSave read_ruby_sapphire_save(const uint8_t* save, std::size_t size) {
  RubySapphireSave out;
  // Two copies of the game, 14 sections of 4 KB each; the newer copy counts. A section's footer:
  // its id (u16 at 0xFF4), checksum (u16 at 0xFF6), signature (0x08012025 at 0xFF8), save count.
  constexpr std::size_t kSection = 0x1000;
  if (!save || size < 28 * kSection) return out;
  auto get16 = [&](std::size_t at) { return static_cast<uint16_t>(save[at] | save[at + 1] << 8); };
  auto get32 = [&](std::size_t at) { return static_cast<uint32_t>(get16(at)) | static_cast<uint32_t>(get16(at + 2)) << 16; };
  uint32_t best_count = 0;
  for (std::size_t slot = 0; slot < 2; ++slot) {
    for (std::size_t s = 0; s < 14; ++s) {
      const std::size_t at = (slot * 14 + s) * kSection;
      if (get32(at + 0xFF8) != 0x08012025u || get16(at + 0xFF4) != 0) continue;
      const uint32_t count = get32(at + 0xFFC);
      if (out.valid && count <= best_count) continue;
      out.valid = true;
      best_count = count;
      out.seed = get16(at + 0xFF6);
      // Section 0 is save block 2: the Pokédex's caught flags at 0x28 (pokeruby, struct Pokedex).
      out.has_jirachi = (save[at + 0x28 + 384 / 8] >> (384 % 8)) & 1;  // national No. 385
    }
  }
  return out;
}

// Emerald's other Mystery Gift cards, whose deliveryman scripts Game Freak left in the game
// (data/scripts/gift_battle_card.inc, gift_stamp_card.inc, gift_trainer.inc) and which were never
// sent out in English. Our own cards and wording; the same workings.
namespace emerald_extra {
constexpr uint16_t kFlagMysteryGiftDone = 0x1E4;
constexpr uint16_t kSpecialValidateEReaderTrainer = 249, kSpecialGetMysteryGiftCardStat = 392;
constexpr uint16_t kVar8008 = 0x8008, kVar8009 = 0x8009, kVarResult = 0x800D;

std::vector<uint8_t> card(uint16_t flag, uint16_t icon, uint32_t id_number, uint8_t type, uint8_t background, uint8_t max_stamps,
                          const std::string& title, const std::array<std::string, 4>& lines, const std::string& foot1,
                          const std::string& foot2) {
  std::vector<uint8_t> c(332, 0);
  c[0] = static_cast<uint8_t>(flag), c[1] = static_cast<uint8_t>(flag >> 8);
  c[2] = static_cast<uint8_t>(icon), c[3] = static_cast<uint8_t>(icon >> 8);
  for (int i = 0; i < 4; ++i) c[4 + static_cast<std::size_t>(i)] = static_cast<uint8_t>(id_number >> (8 * i));
  c[8] = static_cast<uint8_t>(type | (background & 0x7) << 2 | 1 << 6);  // backgrounds 0-7; friends may be sent it
  c[9] = max_stamps;
  put_text(c, 10, title);
  put_text(c, 50, "PROJECT PROMETHEUS EVENT");
  for (std::size_t line = 0; line < 4; ++line) put_text(c, 90 + 40 * line, lines[line]);
  put_text(c, 250, foot1);
  put_text(c, 290, foot2);
  return c;
}

// The Visiting Trainer: the card's deliveryman tells the password for the survey; sent through
// MYSTERY GIFT again, the survey brings the trainer to the Mystery Events House in Sootopolis City.
std::vector<uint8_t> visiting_script() {
  ScriptWriter s;
  s.setvaddress();
  s.specialvar(kVarResult, kSpecialValidateEReaderTrainer);
  s.vgoto_if_result(0, "arrived");
  s.lock();
  s.faceplayer();
  s.say("password");
  s.release();
  s.end();
  s.label("arrived");
  s.lock();
  s.faceplayer();
  s.say("arrived");
  s.release();
  s.end();
  s.text("password", "With this WONDER CARD, you can\ninvite a TRAINER to SOOTOPOLIS CITY.\f"
                     "Fill in the survey at any POKéMON\nMART with these words:\f"
                     "GIVE ME\nAWESOME TRAINER\f"
                     "Then use MYSTERY GIFT's WIRELESS\nCOMMUNICATION again.");
  s.text("arrived", "A TRAINER is waiting for you in\nSOOTOPOLIS CITY: the house southeast\fof the POKéMON CENTER. Enjoy the\nbattle!");
  return s.finish();
}

// The Stamp Card: one stamp a visit, until the card is full.
std::vector<uint8_t> stamp_script() {
  ScriptWriter s;
  s.setvaddress();
  s.op(0x1A), s.u16(kVarResult), s.u16(1);  // setorcopyvar VAR_RESULT, GET_MAX_STAMPS
  s.specialvar(kVar8008, kSpecialGetMysteryGiftCardStat);
  s.op(0x1A), s.u16(kVarResult), s.u16(0);  // GET_NUM_STAMPS
  s.specialvar(kVar8009, kSpecialGetMysteryGiftCardStat);
  s.op(0x18), s.u16(kVar8008), s.u16(kVar8009);  // subvar
  s.op(0x83), s.op(0), s.u16(kVar8008);          // buffernumberstring STR_VAR_1
  s.lock();
  s.faceplayer();
  s.say("left");
  s.release();
  s.end();
  s.text("left", "Thanks for collecting STAMPS!\f{STR_VAR_1} more to fill your STAMP CARD.\n"
                 "Get one each time you visit.");
  return s.finish();
}

// The Battle Card: three link battles won against TRAINERS holding the same card, then a prize.
std::vector<uint8_t> battle_card_script() {
  ScriptWriter s;
  s.setvaddress();
  s.vgoto_if_set(kFlagMysteryGiftDone, "info");
  s.op(0x1A), s.u16(kVarResult), s.u16(2);  // GET_CARD_BATTLES_WON
  s.specialvar(kVar8008, kSpecialGetMysteryGiftCardStat);
  s.op(0x21), s.u16(kVar8008), s.u16(3);    // compare_var_to_value: three wins
  s.op(0xBB), s.op(5), s.ref("info");       // vgoto_if NE
  s.lock();
  s.faceplayer();
  s.say("prize");
  s.giveitem(13);  // ITEM_POTION, as Game Freak's own card gave
  s.release();
  s.setflag(kFlagMysteryGiftDone);
  s.end();
  s.label("info");
  s.lock();
  s.faceplayer();
  s.say("info");
  s.release();
  s.end();
  s.text("info", "Your BATTLE CARD counts your link\nbattles with TRAINERS who hold\fthe same CARD. Win three for a\nprize!");
  s.text("prize", "Three wins on your BATTLE CARD!\nHere is your prize.");
  return s.finish();
}

const uint16_t kPassword[4] = {0x1602, 0x0A14, 0x2017, 0x020B};  // GIVE ME AWESOME TRAINER
}  // namespace emerald_extra

std::vector<EventGift> event_gifts(const std::string& game_code) {
  std::vector<EventGift> out;
  if (game_code == "AXVE" || game_code == "AXPE") {
    EventGift eon{"eon", "Eon Ticket", "Ferry to Southern Island (Latias in Ruby, Latios in Sapphire). Dad gives it; sail from Lilycove City after the Hall of Fame", "", "", 0, {}, eon_ticket_event()};
    eon.cable = true;
    out.push_back(std::move(eon));
    auto cable = [&](EventGift g) {
      g.cable = true;
      out.push_back(std::move(g));
    };
    cable({"enigma", "Enigma Berry", "The event-only berry, with its own picture. Dad gives it at the Petalburg City Gym; plant it to grow more", "", "", 0, {},
           enigma_berry_event()});
    cable({"clock", "Clock fix", "Nintendo's berry-clock fix. Then hold B, Select and Left on the title screen to set the game's clock", "", "", 0, {},
           clock_fix_event()});
    cable({"words", "Rare words", "All 33 rare easy chat words (trendy sayings), for your profile, trends and greetings", "", "", 0, {}, rare_words_event()});
    // The script is made for the player's own save when the event opens (jirachi_event).
    cable({"jirachi", "Jirachi", "The WISHMKR Jirachi of the Pokémon Colosseum Bonus Disc, straight into your party. One per save", "", "", 0, {}, jirachi_event(0)});
    for (uint8_t level : {50, 100}) {
      for (const ECardTrainer& t : kECardTrainers) {
        std::string name = t.name;
        for (std::size_t i = 1; i < name.size(); ++i) name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
        cable({std::string("ecard-") + t.key + "-" + std::to_string(level), "Battle e-Card",
               "Battle Tower: your first foe in a new challenge", "ecard", name + ", Lv. " + std::to_string(level), 0, {},
               ecard_trainer_event(t, level)});
      }
    }
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
    out.push_back({"pichu", "Pichu Egg", "A Pichu egg that knows Surf, from an event held only in Japan. The deliveryman gives it; keep a party slot free", "", "",
                   kPichuCardFlag, pichu_card(id++), pichu_script()});
  }
  if (emerald) {
    namespace ex = emerald_extra;
    uint32_t extra_id = 100;  // card numbers apart from the others
    uint16_t flag = 1014;
    for (const ECardTrainer& t : kECardTrainers) {
      std::string name = t.name;
      for (std::size_t i = 1; i < name.size(); ++i) name[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[i])));
      EventGift g{std::string("visiting-") + t.key, "Visiting Trainer", "Battle them in Sootopolis City, after a survey password",
                  "visiting", name + ", Lv. 50", flag,
                  ex::card(flag, t.party[0].species, extra_id++, 0, 6, 0, "VISITING TRAINER",
                           {std::string("A TRAINER named ") + t.name + " wants", "to battle you in SOOTOPOLIS.",
                            "Ask the deliveryman in any", "POKéMON CENTER how to invite them."},
                           "In SOOTOPOLIS CITY: the house", "southeast of the POKéMON CENTER."),
                  ex::visiting_script()};
      g.password.assign(std::begin(ex::kPassword), std::end(ex::kPassword));
      g.trainer = ecard_trainer(t, 50, t.emerald_class);
      out.push_back(std::move(g));
      ++flag;
    }
    EventGift stamp{"stamp", "Stamp Card", "A Wonder Card with seven stamps to collect: connect again to get each next one", "", "", 1017,
                    ex::card(1017, 25, extra_id++, 1, 7, 7, "STAMP CARD",
                             {"Collect seven STAMPS of HOENN's", "POKéMON: connect to MYSTERY GIFT",
                              "again to get the next one.", ""},
                             "One STAMP each visit.", ""),
                    ex::stamp_script()};
    for (uint32_t i = 0; i < 7; ++i) {
      static const uint16_t kSpecies[7] = {277, 280, 283, 25, 353, 354, 350};  // Treecko, Torchic, Mudkip, Pikachu, Plusle, Minun, Azurill
      stamp.stamps.push_back(kSpecies[i] | (i + 1) << 16);
    }
    out.push_back(std::move(stamp));
    out.push_back({"battlecard", "Battle Card", "Counts your link battles with friends who hold it too; three wins earn a prize from the deliveryman", "", "", 1018,
                   ex::card(1018, 384, extra_id++, 2, 0, 0, "BATTLE CARD",
                            {"Battle friends who hold this", "CARD over the link. The card",
                             "keeps the score. Win three", "for a prize."},
                            "Show your record to the", "deliveryman in any POKéMON CENTER."),
                   ex::battle_card_script()});
  }
  // One card id per set, so choosing another set is a new card (the game offers to swap it).
  const CaveGameDef& cave = emerald ? kEmeraldCave : kFireRedLeafGreenCave;
  for (uint16_t i = 0; i < std::size(kAlteringCave); ++i) {
    const CaveSetDef& c = kAlteringCave[i];
    const uint16_t set = c.species == 41 ? 0 : static_cast<uint16_t>(i + 1);
    const uint16_t flag = static_cast<uint16_t>(1004 + i);
    out.push_back({std::string("altering-") + c.key, "Altering Cave",
                   std::string("Wild ") + c.name + " in the Altering Cave (" + cave.place_name + ")", "altering", c.name, flag,
                   cave_card(c, flag, id++, cave), cave_script(c, set, cave)});
  }
  return out;
}

}  // namespace p64
