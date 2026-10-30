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
- **Ruby and Sapphire** have both events too (different addresses: the pret Ruby decompilation and the games are needed). FireRed and LeafGreen have neither.
