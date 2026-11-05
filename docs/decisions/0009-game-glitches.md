# 0009: The games' own glitches are allowed; the app never edits a game

Status: accepted, 2026-11-02 (the owner's decision).

## The question

Competitive training needs TMs that the Generation 3 games give only once per save (22 of the 50:
Dragon Claw, Water Pulse, Calm Mind, Toxic, Hail, Bulk Up, Bullet Seed, Taunt, Rain Dance, Solar Beam,
Shock Wave, Sludge Bomb, Sandstorm, Rock Tomb, Aerial Ace, Torment, Facade, Thief, Steel Wing, Skill
Swap, Snatch, Overheat). The other 28 can be had again from shops, game corners, the Trainer Hill, the
Pacifidlog fan club or Pickup (listed from the pret decompilations of all five games). No legitimate
route farms the 22: a fresh save gives one more of each, Emerald's Trainer Hill e-Reader cards would
add only Toxic, Sludge Bomb, Torment and Skill Swap.

## Decision

- **A glitch in the game's own code is not a cheat.** The owner: "as long as it's the game's own code
  doing it, let's exclude it as a cheat". The app still never writes to a game's memory or save to
  give the player anything; it doesn't automate glitches either.
- **Copying a save from outside the game is a cheat** and isn't offered (a "temporary cloned
  cartridge" of the current save was considered and set aside for that reason).
- **The app makes the glitch safe:** a GBA save state now also keeps the game's save (`slotN.sav` next
  to the state), and loading the state puts the save back too (the save it replaces stays in the
  library's history). A save state before the glitch is a full undo.
- **Documented in the README** (*Training competitive Pokémon*), with disposable wild carriers so the
  team never moves.

## The glitch (Emerald only)

The Battle Tower's Link Multi Battle challenge saves with `SAVE_LINK` (party, bag, everything but the
PC boxes), then offers a full save the player may decline (`tower_save` in
`BattleFrontier_BattleTowerLobby`, marked in pret's pokeemerald as fixed only under `BUGFIX`). Declining
and resetting leaves the party newer than the PC: Pokémon withdrawn since the last full save exist
twice, with their held items; Pokémon deposited since then are gone. Ruby, Sapphire, FireRed and
LeafGreen use the same party-only save only after link play, where it isn't exposed.

Checked in the test bench (`p64_giftsim`) on English Emerald (BPEE), with a test copy of a save in the
Battle Tower lobby: a carrier holding TM30 withdrawn after the last full save; CHALLENGE, LV. 50, two
entrants, YES to the save before entering, NO to the full save, reset, CONTINUE. The carrier was then in
the party and in PC box 1, both holding TM30, both passing the game's checksum. The bench gained
`putat`/`dumpat` (memory at an address the game keeps in a pointer) for it.

Checked in the app: F5, a change written into the game's save, F7: the save came back byte for byte
(without the F7, the change stays).
