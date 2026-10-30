# To build

What's agreed with the owner and not built yet, with the plan for each. In the order to build.

## 1. Jirachi for Ruby and Sapphire

**What it was.** Jirachi came to Generation 3 from GameCube discs: the Pokémon Colosseum Bonus Disc (North America; "WISHMKR" Jirachi) and Pokémon Channel (Europe and Australia). The GBA was connected to the GameCube with a link cable; the disc sent a small program to the GBA, which wrote Jirachi into a Ruby or Sapphire save. One Jirachi per save, from either. Neither worked with Emerald (as far as could be checked; a later Emerald-compatible event is claimed by some sources but not confirmed), so Emerald, FireRed and LeafGreen players traded it over from Ruby or Sapphire.

**The plan.**
- Deliver it to **Ruby and Sapphire** as a **Mystery Event over the link cable**, through the e-Reader the app already plays (`cable_station.cpp`, which delivers the Eon Ticket). Ruby/Sapphire's Mystery Event script has a `givepokemon` command (pokeruby, `MEScrCmd_givepokemon`): the game itself puts the Pokémon in the party, checks for room, marks it seen and caught in the Pokédex, and shows its own "sent over" message. The app still never writes to the save.
- The Jirachi carries the original distribution's data: OT WISHMKR, ID 20043, level 5, the event's moves, Poké Ball, met location and fateful encounter mark. Before building, research the exact data and how the original program generated Jirachi's personality value and IVs, so each one could have come from the real disc. Offer the Pokémon Channel Jirachi too if its data can be pinned down the same way.
- **Not** done by emulating the GameCube and replaying the disc's program: that needs Nintendo's program from the disc, which the app neither has nor distributes, and it writes the save from outside.
- **Emerald, FireRed and LeafGreen** get Jirachi as players did: by trade from Ruby or Sapphire (see 2).
- **Checks:** received in Ruby and Sapphire; the Pokémon's data decoded from the party (species, OT, ID, moves, met location, fateful bit, checksum); full-party message; once per save.

Not chosen: a Mystery Gift Jirachi straight into Emerald (possible with Emerald's `givemon`, as for the Pichu egg), because it would be a new event rather than a restored one, and would carry the player's own name as OT instead of WISHMKR.

## 2. Local trade between your own games

**Why.** Trading is how Generation 3 moved event Pokémon between games (Jirachi from Ruby to Emerald, for one), and today link play needs two computers or the online relay.

**The plan.** Run two GBA games side by side in one app window (two gpSP cores), joined by a virtual link cable: gpSP's netpacket messages passed between the two cores in memory instead of over the relay, in either mode (the Pokémon cable mode, or the Wireless Adapter for FireRed, LeafGreen and Emerald). Each game keeps its own save in the library. One screen split in two, or the second game in a smaller picture; the controller drives the game in focus. Then the Cable Club works as on two consoles.

## 3. Ruby and Sapphire: more of the e-Reader

The same cable station and Mystery Event script, with other commands (pokeruby, `mystery_event_script.c`):
- **Enigma Berry** (`setenigmaberry`): the event-only berry.
- **The clock reset** (`enableresetrtc`): what Nintendo distributed to fix Ruby and Sapphire's berry-clock bug.
- **Battle e-Card trainers** (`addtrainer`): extra Battle Tower opponents.
- Then ribbons and rare trendy words, if worthwhile.

## 4. Emerald: the e-Reader menu and small Mystery Gift extras

- Emerald's own **e-Reader** option (Trainer Hill e-Cards): a separate protocol, over the link cable.
- **Visiting Trainer**, **Battle Card** and **Stamp Card** (Emerald's Mystery Gift scripts still in the game).

## 5. Checking in real play

With saves past the Hall of Fame (and into the Sevii Islands for FireRed and LeafGreen): sail with each ticket and meet the island's Pokémon; walk into the Altering Cave after its gift. See [`verify.md`](verify.md).
