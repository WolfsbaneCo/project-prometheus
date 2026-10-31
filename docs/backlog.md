# To build

What's agreed with the owner and not built yet, with the plan for each.

## Emerald's e-Reader menu (Trainer Hill e-Cards)

Everything else on this list was built on 2026-10-30 (see [decision 0008](decisions/0008-event-gifts.md)): Jirachi, local trade, the rest of Ruby and Sapphire's e-Reader, and Emerald's Visiting Trainer, Stamp Card and Battle Card.

**What it was.** Emerald's title menu shows **e-Reader** in place of Mystery Gift when no Wireless Adapter is plugged in. Japanese e-Reader cards added trainers to the **Trainer Hill** on Route 111.

**Why it's separate.** Emerald talks to the e-Reader differently from Ruby and Sapphire (`ereader_helpers.c` in pret's pokeemerald): it first sends the e-Reader a program in multiplayer mode at 115,200 bps, then receives the card's Trainer Hill set (`struct EReaderTrainerHillSet`, 3,840 bytes, checksummed) in the GBA's 32-bit normal serial mode. gpSP's link messages carry only the multiplayer cable and the Wireless Adapter, so the normal-mode exchange reaches nothing.

**The plan.**
1. A small change to gpSP's serial emulation (`serial.c`): a normal-mode transfer handed to the frontend, as it already does for the Wireless Adapter (`rfu_transfer`) and the Game Boy Player; applied as a patch to the pinned gpSP in `third_party/gpsp/CMakeLists.txt`.
2. The e-Reader's side of the exchange in the app: accept the program upload, then send a Trainer Hill set as the e-Reader's program would after scanning a card.
3. Our own Trainer Hill floors (trainers and teams, as for the Battle e-Cards), with the game's checksums; the game saves them to its flash (`TryWriteTrainerHill`) and offers them at the Trainer Hill.
4. Checks: the transfer, the game's validation, the Trainer Hill using the new floors.

## Checking in real play

With saves past the Hall of Fame (and into the Sevii Islands for FireRed and LeafGreen): sail with each ticket and meet the island's Pokémon; walk into the Altering Cave after its gift; battle a Battle e-Card trainer and a Visiting Trainer; trade between two games linked on one computer. See [`verify.md`](verify.md).
