# 0008: Event gifts

- **Status:** Accepted (implemented in `apps/prometheus64` for Pokémon Emerald, English).
- **Date:** 2026-10-30
- **Owner's choices:** no cheats: the gifts come "by recreating whatever the requirements were". The events themselves no longer exist, so the app stands in for them. Every ticket the game can use, whatever its region (the Old Sea Map was only handed out for the Japanese Emerald).

## What players did then

At an event or a store, a distribution machine (a GBA with a Wireless Adapter) broadcast a Wonder Card. The player opened **Mystery Gift › Wonder Cards › Wireless Communication** in their own game, which received the card and a short script and saved them. Later, the deliveryman upstairs in any Pokémon Center ran that script: he handed over the ticket and opened the ship route, which the Lilycove City harbor then sailed. Mystery Gift itself was unlocked once, with the phrase LINK TOGETHER WITH ALL in a Poké Mart's questionnaire.

## What the app does

Exactly the machine's part. The game does all of its own part, unchanged; the app never writes to the game's memory or its save.

| Layer | Where | What it is |
|---|---|---|
| Adapter | `src/gift_station.cpp` | gpSP emulates the Wireless Adapter and exchanges its messages through the libretro netpacket interface. The station answers in the app (no network): it broadcasts as a Wonder Card distributor (serial number `0x7F7D`), accepts the game's connection and exchanges data every frame |
| Nintendo's wireless library | same | Each frame is a packet of "LL frames": the reliable transfer (the game's name when it connects; the host's "you may join" reply) and the per-frame unreliable one carrying the link layer |
| Game Freak's link layer | same | Every player's seven command words each frame, echoed by the host to everyone; player numbers, the 200-byte player data swap ("GameFreak inc."), blocks in 12-byte pieces, the "ready" handshakes |
| Mystery Gift | same | The server's script, as the games' own server runs it: ask for the game's data, check its Wonder Card (none, this one, another one: then the game asks to replace it), send the card and the deliveryman's script, wait for the game to finish |
| The gifts | `src/gift_catalog.cpp` | Our own Wonder Cards and deliveryman scripts, in the games' formats and character set, with our own wording. Each script does what the event's did: checks the bag and flags, gives the item, sets the received flag and the ship flag |

The pret decompilation of Emerald (`src/librfu_rfu.c`, `src/link_rfu_2.c`, `src/union_room.c`, `src/mystery_gift_*.c`, `data/scripts/`) documents every layer; the code here is our own, written from it.

| Gift | Item | Ship flag | Card |
|---|---|---|---|
| Aurora Ticket | 371 | `FLAG_ENABLE_SHIP_BIRTH_ISLAND` | 1000 |
| Mystic Ticket | 370 | `FLAG_ENABLE_SHIP_NAVEL_ROCK` | 1001 |
| Old Sea Map | 376 | `FLAG_ENABLE_SHIP_FARAWAY_ISLAND` | 1002 |
| Eon Ticket | 275 | `FLAG_ENABLE_SHIP_SOUTHERN_ISLAND` | 1003 |

## Choices and why

- **The machine, not the save.** Writing the ticket into the save would be quicker but is exactly what the owner asked not to do. Playing the machine keeps every step in the game: Mystery Gift, the Wonder Card, the deliveryman, the harbor.
- **In the app, no network.** The station talks to gpSP directly, so it needs no group key, relay or internet.
- **The game restarts to open an event.** gpSP picks its link hardware (cable or Wireless Adapter) when a game starts, and Mystery Gift is on the title menu anyway. Closing an event doesn't restart the game; the next link switches the hardware back, keeping the game's progress.
- **One card at a time,** as the games always worked: a new card replaces the old one (the game asks first, and warns when its gift hasn't been collected).
- **A test bench.** `tools/giftsim.cpp` plays a GBA game without a window at thousands of frames a second, scripted; the whole exchange was developed and checked against Emerald with it.

## Not yet

- **FireRed and LeafGreen** (Aurora and Mystic Tickets): the same machine, with FRLG's flags, items and script commands; needs one of those games to test with.
- **Ruby and Sapphire** (Eon Ticket): they received it as an e-Reader Mystery Event over the link cable, a different path.
- **Other Emerald languages:** the same flags and items, but untested.

## Daily hints (2026-10-30)

The owner asked for reminders on the days the two luck events of Emerald would pay, since the odds are close to zero otherwise: Mirage Island (about 1 in 65,536 per party Pokémon per day) and the Lilycove Lottery Corner (5 matching digits for the Master Ball: about 1 in 100,000). `src/pokemon_hints.cpp` reads the running game, as a guide would, and never writes to it, so the odds stay the game's:

- **Mirage Island:** `VAR_MIRAGE_RND_H` against the low 16 bits of each party Pokémon's personality value (`IsMirageIslandPresent`).
- **Lottery:** `VAR_POKELOT_RND1` against each party and PC Pokémon's trainer ID, matching decimal digits from the right, eggs left out, nothing when `FLAG_DAILY_PICKED_LOTO_TICKET` says today's ticket is drawn (`PickLotteryCornerTicket`). 2 to 5 digits: PP Up, Exp. Share, Max Revive, Master Ball.
- **Addresses** (English Emerald): `gSaveBlock1Ptr` 0x03005D8C, `gPokemonStoragePtr` 0x03005D94, `gPlayerPartyCount` 0x020244E9, `gPlayerParty` 0x020244EC; the game's own code refers to them 829, 30, 41 and 1,091 times.
- **In the app:** checked every 5 seconds of play; each lucky day is told once (the keys are kept per game, `hints_seen`), as a 10-second message. Off during link play, online play and events. **Daily hints** in the in-game menu turns them off.
- **Checked** with the test bench on Emerald memory (`hints` command): nothing on an ordinary day; Mirage Island when the number matches; Max Revive for 4 digits on a party Pokémon, Master Ball for 5 on a PC one; no lottery hint once the ticket is drawn.
- **Ruby and Sapphire** (2026-10-30): the same rules and variables (pret's pokeruby). Their save block and PC sit at fixed addresses (`gSaveBlock1` 0x02025734, `gPokemonStorage` 0x020300A0), the party in IWRAM (`gPlayerParty` 0x03004360, count 0x03004350), flags at 0x1220 and vars at 0x1340 in the save block, `FLAG_DAILY_PICKED_LOTTO_TICKET` 0x8CA; the same in Ruby 1.0 and Sapphire 1.2 (the games' code refers to them 339, 23, 697 and 60 times). Checked against planted memory in both games, with the same six cases as Emerald.
- FireRed and LeafGreen have neither event.

## Altering Cave (2026-10-30)

Emerald's Altering Cave (Route 103) has only Zubat, unless the game receives a Mystery Gift that moves it to one of eight other sets of wild Pokémon from Johto: Mareep, Pineco, Houndour, Teddiursa, Aipom, Shuckle, Stantler, Smeargle (`gAlteringCave2`-`9`). The game still contains Game Freak's script for it (`data/scripts/gift_altering_cave.inc`, which moves to the next set each time); it was never distributed outside Japan.

- **What the app sends:** a Wonder Card and a deliveryman script, like the tickets. The script sets `VAR_ALTERING_CAVE_WILD_SET` to the chosen set (0 = Zubat again), which is what the game reads when it picks the cave's wild Pokémon, and tells the player in our own words.
- **Choosing:** one row on the Event gifts page, "Altering Cave", with < > to pick the Pokémon. Each set has its own card id (1004-1012), so choosing another set is a new card, which the game offers to swap.
- **Checked** (test bench, Emerald): the game received the Mareep card through Mystery Gift (swapping out a card whose gift was waiting), saved it; upstairs in the Pokémon Center the deliveryman said our text, and `VAR_ALTERING_CAVE_WILD_SET` went from 0 to 1. Not yet seen: a wild Mareep in the cave (the test save has no Pokémon to walk there with).

## The Surf Pichu egg (2026-10-30)

A Japanese Mystery Gift event: a Pichu egg whose Pichu knows Surf. Emerald still has its script (`data/scripts/gift_pichu.inc`); it was never distributed anywhere else. The app's deliveryman script takes the same steps with our own wording: once per card (`FLAG_MYSTERY_GIFT_DONE`), if the party has room, `giveegg SPECIES_PICHU`, marked as an event Pokémon (`setmodernfatefulencounter`, met location 0xFF "fateful encounter"), Surf as its third move. Game Freak's script sets Surf for party slots 2 to 6 only (a player always had a Pokémon); slot 1 is covered too. Card id 1013.

**Checked** (test bench, Emerald): the card received through Mystery Gift; the deliveryman's text; the egg decoded from the party: Pichu, an egg, met location 0xFF, the fateful-encounter bit, moves ThunderShock, Charm, Surf, valid checksum, both with an empty party (slot 1) and with a Pokémon already in it (slot 2).

## FireRed and LeafGreen (2026-10-30)

FireRed and LeafGreen (English, `BPRE`, `BPGE`) receive Mystery Gift exactly as Emerald does, compared in the pret decompilations: the Wonder Distributor serial (0x7F7D), the broadcast's game data (its version field is written but never checked), the client's commands, the link game data, the Wonder Card and the saved RAM script's layout, and all 25 script commands the app's scripts use have the same numbers. So the same station serves them; only the catalog differs:

- **Aurora Ticket** (Birth Island, Deoxys) and **Mystic Ticket** (Navel Rock, Lugia and Ho-Oh), the two events these games had: items 0x173 and 0x172 as in Emerald; their own received flags (0x2A7, 0x2A8), ferry flags (`FLAG_ENABLE_SHIP_BIRTH_ISLAND` 0x84B, `FLAG_ENABLE_SHIP_NAVEL_ROCK` 0x84A) and fought flags (0x2E4; 0x2F2, 0x2F3). The deliveryman sends the player to the Vermilion City harbor, where the Seagallop ferry sails once the Sevii Islands are open.
- **Altering Cave** on Six Island: the same eight other sets, in the same order, through `VAR_ALTERING_CAVE_WILD_SET` (0x4024 in these games).
- Mystery Gift is unlocked by the same questionnaire phrase, in any Poké Mart.

**Checked** with LeafGreen (test bench; a new game, Mystery Gift switched on in a test copy and the player placed upstairs in Viridian City's Pokémon Center): each card received over the Wireless Adapter and saved; the deliveryman's text; the Aurora Ticket in the Key Items pocket with 0x2A7 and 0x84B set; the Mystic Ticket with 0x2A8 and 0x84A set; Altering Cave set to Smeargle's (8). **FireRed** (1.1, the owner's copy; the same RAM addresses as LeafGreen 1.0): the same test, Aurora Ticket received and handed over, its flags set.

## Ruby and Sapphire: the Eon Ticket over the link cable (2026-10-30)

Ruby and Sapphire have no Mystery Gift. Their Eon Ticket came from an e-Reader card, read into the game's **Mystery Events** menu over the link cable. The app plays the e-Reader (`src/cable_station.cpp`), written from pret's pokeruby:

- **The cable:** gpSP's Pokémon cable mode (`mul_poke`, its "MPK1" netpacket messages: a device's state and 8 command words per link frame). The game must be the master Game Boy (`gLinkStatus` master bit, checked by the menu), so it is netpacket id 0 and the station id 1; gpSP's own handshake (0xB9A0 / 0x8FFF) makes the link.
- **Game Freak's link layer** (`link.c`): when the master asks for player data (0x2222), the station sends its `LinkPlayerBlock` ("GameFreak inc.", the game's link type 0x5501, English), as a block (0xBBBB, then 0x8888 frames of 14 bytes); once the game's block has arrived, it sends the event as a block, which the game keeps in its shared buffer (over 256 bytes); then it answers the game's close (0x5FFF), which the game waits for from everyone before it runs the event.
- **The event** (`gift_catalog.cpp`): a Mystery Event script laid out as Game Freak's own test event for the Eon Ticket (`data/debug_mystery_event_scripts.s`): `checkcompat` (English; Ruby and Sapphire), a CRC-16 over the rest, `initramscript` putting a script on DAD in the Petalburg City Gym (map 8.1, object 1), the ticket as a record mixing gift, and a script run at once that sets the game's message ("Go and see your DAD at the GYM in PETALBURG CITY.") and refuses a second load. DAD's script gives the Eon Ticket, sets `FLAG_SYS_HAS_EON_TICKET` (0x853) and removes itself (`killscript`). Our own wording. The script commands it uses have the same numbers as Emerald's.
- **Unlocking Mystery Events** in these games: the Mystery Event Club, in any Pokémon Center: after the 5th badge, give your profile as MYSTERY EVENT IS EXCITING.
- **In the app:** the same Event gifts page; Ruby and Sapphire's event restarts the game with the link cable (gpSP `mul_poke`) and the banner says MYSTERY EVENTS.

**Checked** (test bench, and in the app for Ruby): Ruby 1.0 and Sapphire 1.2, new games with Mystery Events switched on in a test copy: the game linked, asked for player data, received the 636-byte event, closed the link, showed our message and saved; DAD (placed in front of him in a test copy) said our text, the Eon Ticket went into the Key Items pocket, 0x853 went from clear to set, and he went back to his own lines. In the app: Event gifts › Open event, MYSTERY EVENTS, A: the same message and "Progress saved". Not yet seen: the ferry to Southern Island (needs the Hall of Fame).
