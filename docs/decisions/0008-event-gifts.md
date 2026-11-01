# 0008: Event gifts

- **Status:** Accepted (implemented in `apps/prometheus64` for Pokémon Emerald, FireRed, LeafGreen, Ruby and Sapphire, English).
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

- **Other languages:** the same flags and items, but untested. (FireRed, LeafGreen, Ruby and Sapphire were added since: see below.)

## Daily hints (2026-10-30)

The owner asked for reminders on the days the two luck events of Emerald would pay, since the odds are close to zero otherwise: Mirage Island (about 1 in 65,536 per party Pokémon per day) and the Lilycove Lottery Corner (5 matching digits for the Master Ball: about 1 in 100,000). `src/pokemon_hints.cpp` reads the running game, as a guide would, and never writes to it, so the odds stay the game's:

- **Mirage Island:** `VAR_MIRAGE_RND_H` against the low 16 bits of each party Pokémon's personality value (`IsMirageIslandPresent`).
- **Lottery:** `VAR_POKELOT_RND1` against each party and PC Pokémon's trainer ID, matching decimal digits from the right, eggs left out, nothing when `FLAG_DAILY_PICKED_LOTO_TICKET` says today's ticket is drawn (`PickLotteryCornerTicket`). 2 to 5 digits: PP Up, Exp. Share, Max Revive, Master Ball.
- **Addresses** (English Emerald): `gSaveBlock1Ptr` 0x03005D8C, `gPokemonStoragePtr` 0x03005D94, `gPlayerPartyCount` 0x020244E9, `gPlayerParty` 0x020244EC; the game's own code refers to them 829, 30, 41 and 1,091 times.
- **In the app:** checked every 5 seconds of play; each lucky day is told once (the keys are kept per game, `hints_seen`), as a 10-second message. Off during link play, online play and events. **Daily hints** in the in-game menu turns them off.
- **Checked** with the test bench on Emerald memory (`hints` command): nothing on an ordinary day; Mirage Island when the number matches; Max Revive for 4 digits on a party Pokémon, Master Ball for 5 on a PC one; no lottery hint once the ticket is drawn.
- **Ruby and Sapphire** (2026-10-30): the same rules and variables (pret's pokeruby). Their save block and PC sit at fixed addresses (`gSaveBlock1` 0x02025734, `gPokemonStorage` 0x020300A0), the party in IWRAM (`gPlayerParty` 0x03004360, count 0x03004350), flags at 0x1220 and vars at 0x1340 in the save block, `FLAG_DAILY_PICKED_LOTTO_TICKET` 0x8CA; the same in Ruby 1.0 and Sapphire 1.2 (the games' code refers to them 339, 23, 697 and 60 times). Checked against planted memory in both games, with the same six cases as Emerald.
- **Mirage Island from the PC** (2026-10-31, the owner's request): the game checks only the party, but a PC Pokémon whose personality matches brings the island out once moved into the party, the same day. When no party Pokémon matches, the hint names the PC one and its box (the box's own name from `struct PokemonStorage`, after the boxes, 9 bytes each; "Box N" when unnamed). Still once a day (the same key). Checked with planted memory in Ruby and Emerald: the PC hint with the box's name, and the party hint taking precedence when both match.
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

## The rest of Ruby and Sapphire's e-Reader, and Jirachi (2026-10-30)

The owner asked for all of the e-Reader's content and for Jirachi. Each is one Mystery Event over the same cable station, built by `MysteryEventWriter` (`gift_catalog.cpp`): `checkcompat` (English; Ruby and Sapphire), a CRC-16, the commands, `end`, then the data they point to (word-aligned). The game runs its own command for each (pokeruby, `mystery_event_script.c`):

| Event | Command | What the game does |
|---|---|---|
| Enigma Berry | `setenigmaberry` (7) | Copies a `struct EnigmaBerry` (1,328 bytes) into the save and sets `VAR_ENIGMA_BERRY_AVAILABLE`; DAD hands one over once his badge is won (his own script) |
| Clock fix | `enableresetrtc` (14) | Sets `VAR_RESET_RTC_ENABLE` to 0x920 and `FLAG_SYS_RESET_RTC_ENABLE`: B + SELECT + Left on the title screen opens the game's Reset RTC screen |
| Rare words | `addrareword` (10) x 33 | `UnlockTrendySaying` for each of the 33 trendy sayings |
| Battle e-Card | `addtrainer` (13) | Copies a `BattleTowerEReaderTrainer` (188 bytes) into the save; the Battle Tower sends it in when the streak and level match (`ShouldBattleEReaderTrainer`), once |
| Jirachi | `givepokemon` (12) | Copies a `struct Pokemon` (and mail) into the party, marks the Pokédex, "POKéMON was sent over!" |

A short script run at once (`runscript`) sets our own message where the command's own would say less (the clock-reset buttons, the Battle Tower level).

- **Events over 256 bytes.** The game reads the event from its shared buffer, where only blocks over 256 bytes land (`link.c`: smaller ones go to the link's own buffer). The clock fix and rare words are tiny, so the writer pads every event past 256 bytes. Found in testing: unpadded, the game ran an empty buffer and said "safely loaded" without doing anything.
- **The Enigma Berry's design.** The game's own name, growth (24 hours a stage, 1 to 2 berries), flavours (40 each) and description ("A completely enigmatic BERRY. / Appears to have the power of stars.") from its berry table; firmness Hard and 155 mm from the later games' Enigma Berry; our own 48x48 picture (a five-lobed violet berry with a gold star and two leaves, drawn in code); held: `HOLD_EFFECT_RESTORE_HP` 30 (as Sitrus in these games), eaten: restores 30 HP. The checksum is the game's (bytes up to the checksum, description pointers as zero). Nintendo's own Enigma Berry cards are not copied.
- **Battle e-Cards.** Our own trainers and teams (no card's data copied): Mira (Cool Trainer, class 9), Dorian (Expert, 34), Selene (Pokémon Ranger, 67), each at Lv. 50 and Lv. 100: six choices. Teams obey the Tower's rules (no banned species, no repeated species or held item); all IVs 31, competitive EVs, natures from the personality (never shiny); easy chat greetings and farewells. Win streak 0: the first battle of a new challenge, the only one a player can always reach without losing a streak (the two levels keep separate streaks). The checksum is the game's (sum of the 46 words before it); the game clears a trainer whose checksum is wrong.
- **Jirachi.** The WISHMKR Jirachi of the Pokémon Colosseum Bonus Disc (North America, 2004), the only English Jirachi for these games: OT WISHMKR (male), ID 20043 (secret ID 0), Lv. 5, Wish, Confusion, Rest, a Poké Ball, met location 255, from Ruby, English, friendship 100, not fateful. The disc made everything else from a 16-bit seed, the checksum of the first section of the game's save, with the games' random numbers (LCRNG 0x41C64E6D, 0x6073): the personality from the next two numbers (high half first), the IVs from the two after, the held berry from the fifth (Salac, or Ganlon when (n / 3) is odd). Documented by PKHeX's legality checker (`Legality/RNG/ClassicEra/Gen3/Wishmkr.cs`, `EncounterGift3.cs`); our generator reproduces PKHeX's nine known shiny seeds' natures and shininess exactly. The app reads the seed from the newest copy of the save's section 0 (read only), so one save gets the same Jirachi, as from the disc. **Once per save:** the game has no flag for it, so the app checks the save's Pokédex (Jirachi caught) and refuses a second. Level and stats are computed (the game takes a sent Pokémon's stats as they are).
- **Ribbons** (`giveribbon`): not offered. They were prizes for tournaments players entered; handing them out would be the kind of shortcut the owner ruled out.
- **The National Pokédex** (`givenationaldex`): not offered; no English event gave it to Ruby and Sapphire.

**Checked** (test bench, Ruby 1.0): every event loaded through MYSTERY EVENTS with our message. Clock fix: 0x402C = 0x920 and 0x862 set; holding B + SELECT + Left on the title screen opens the game's "Reset RTC?" screen. Rare words: the 33 bits set. Enigma Berry: the berry in the save with a valid checksum; DAD (with his badge won in a test copy) said "I just received a very rare BERRY" and gave it; the Berries pocket's tag shows our picture, "Hard", 6.1", and the description. Battle e-Card (Mira, Lv. 50): the trainer in the save, kept by the game's own validation. Jirachi: in the party, decoded: species 409, Wish/Confusion/Rest, met 255, valid checksum, OT WISHMKR; loading again with that save is refused (Jirachi caught). Not yet seen: a Battle Tower battle against an e-Card trainer (needs a save past the Hall of Fame).

## Emerald's Visiting Trainer, Stamp Card and Battle Card (2026-10-30)

Emerald still has Game Freak's deliveryman scripts for three Wonder Cards never sent out in English (`data/scripts/gift_trainer.inc`, `gift_stamp_card.inc`, `gift_battle_card.inc`). The app sends our own cards and scripts that work the same way:

- **Visiting Trainer** (cards 1014-1016: Mira, Dorian, Selene, Lv. 50, Emerald's facility classes 0x12, 0x09, 0x45). The deliveryman gives the survey password GIVE ME AWESOME TRAINER. With the card held and those words in the questionnaire (`mysteryGift.questionnaireWords`), the station's next session sends the e-Reader trainer (`CLI_RECV_EREADER_TRAINER`, link id 26); "A new TRAINER has arrived." The Mystery Events House in Sootopolis City then opens its stairs to the battle (`frontier_checkvisittrainer`).
- **Stamp Card** (card 1017, type 1, 7 stamps). Each later session with the card held sends the next stamp the card doesn't have (`CLI_SAVE_STAMP`, link id 24; species and an id 1-7): Treecko, Torchic, Mudkip, Pikachu, Plusle, Minun, Azurill. No prize, as Game Freak's card.
- **Battle Card** (card 1018, type 2). The game counts link battles with players holding the same card; the deliveryman gives the prize after three wins (a Potion, as Game Freak's).
- **The game's data** (`MysteryGiftLinkGameData`) has the card's metadata word-aligned, at offset 32 (stamps at 40 and 54, the stamp count at 68): found from the game's own bytes.
- **Card backgrounds** are 0 to 7 (`NUM_WONDER_BGS`); a card with 8 is refused by the game without a word (found in testing).

**Checked** (test bench, Emerald): the Stamp Card received, then two more sessions: "A new STAMP has been received", Treecko then Torchic on the card. A Visiting Trainer card received, the questionnaire words set in a test copy, the next session: "A new TRAINER has arrived.", the trainer in the save (class 0x12, Starmie/Snorlax/Gardevoir, Lv. 50, valid checksum). The Battle Card received and shown. Not yet seen: the battle in Sootopolis City, and a Battle Card's three wins.

**Not built: Emerald's e-Reader menu (Trainer Hill e-Cards).** Emerald talks to the e-Reader differently: it sends the e-Reader a program at 115,200 bps in multiplayer mode, then receives the card's Trainer Hill data (a 3,840-byte set) in the GBA's 32-bit normal serial mode (`ereader_helpers.c`). gpSP's netpacket link carries only the multiplayer cable and the Wireless Adapter; normal mode reaches nothing. It needs a change inside gpSP's serial emulation plus the e-Reader's side of that exchange: a separate piece of work (`docs/backlog.md`).

## Two games linked on one computer (2026-10-30)

For trading between the player's own games (Jirachi from Ruby into Emerald, for one). `src/local_link.cpp`:

- **A second core.** gpSP keeps its whole state in globals, so a second game needs a second copy of the library: the app copies `gpsp_libretro` to `cores/gpsp_libretro-second` in its own folder and loads that (a different file is loaded separately on Linux and, with a different name, on Windows). `Core`'s callbacks follow whichever core was called last (`g_core`), so each core's picture, sound and input go to the right place.
- **The link.** The first game is the host (netpacket id 0), the second the guest (1); each core's messages are queued for the other and handed over before it runs its next frame, or when it polls. The same messages gpSP sends over the relay, so cable and wireless both work. Both games use the same link: the cable, unless both are set to wireless.
- **In the app:** **Link a second game** in a GBA game's menu (pick with < >). The two games side by side, a label over each; F3 (or **Controlling**) moves the controls and the sound to the other. Each game's save is kept in the library as usual. Not two copies of one game (they'd share one save).

**Checked:** in the test bench, two Emerald saves linked over the Wireless Adapter: one sent its Wonder Card (MYSTERY GIFT › WONDER CARDS › SEND), the other received it from FRIEND: "A WONDER CARD has been received from AAAAAAA." In the app (Linux): Emerald and Ruby side by side, each game's own picture, F3 moving the controls. Not yet seen: a Cable Club trade between two games (needs saves at a Pokémon Center), and the Windows build.

## Pokémon stats: the party's EVs (2026-11-01)

The owner asked to see EVs, which the games never show, but only when asked for, and then simply: pick a Pokémon in the party and see how its EVs are spread (an earlier idea, searching by the summary screen's IDNo, was dropped: that number is the trainer's, the same on every Pokémon they caught). **Pokémon stats** in a GBA Pokémon game's in-game menu opens a page listing the party; picking one (click, or the arrow keys) shows its EVs stat by stat, the total out of 510 and what's left, its IVs and its nature (the raised and lowered stats marked). To keep the menu its size, the Speed-up row moved to the Controls screen (**Speed-up amount**, under the speed key).

- `src/pokemon_stats.cpp` reads `gPlayerPartyCount` and `gPlayerParty` (Ruby and Sapphire at the same addresses as the daily hints; Emerald likewise; FireRed and LeafGreen at 0x02024029 and 0x02024284, the same in both memory layouts the decompilation builds: checked on FireRed 1.1 and LeafGreen 1.0) only while the page is drawn, and never writes. Each Pokémon is decrypted with its personality value and trainer ID, its four blocks put in the order its personality gives, and checked against its checksum; empty slots, eggs and anything that fails the checksum are left out. Species names and National Pokédex numbers come from the pret tables (`src/pokemon_species.inc`, generated, with the male and female signs of Nidoran written as M and F, which the app's font lacks).
- With two games linked here, the page shows the party of the game being controlled.
- **Checked** in the test bench (`mons` command) on Ruby, Sapphire, Emerald, FireRed and LeafGreen: the WISHMKR Jirachi from Ruby's Mystery Event read back as Jirachi, Lv. 5, OT WISHMKR; a copy of it with 6 HP / 252 Attack / 252 Speed EVs, written into test copies of each game's save (the game itself then loads it into its party, so the addresses are the games' own), read back exactly, 510 in all. And in the app (Emerald): the page, the picking, and the empty party before a save is continued. `tests/pokemon_stats_tests.cpp` checks the decoding on a Pokémon built in the test.
