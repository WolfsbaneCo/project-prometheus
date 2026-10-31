# Project Prometheus

**Project Prometheus** is a free desktop player for the N64 and GBA games you own: a sharp picture on big screens, simple controls, couch and online multiplayer, GBA link play, and a save library backed up to your own Google Drive.

It contains no games and never downloads, finds or shares them.

> **Project Prometheus is free, and the only official download is this repository's [Releases](https://github.com/WolfsbaneCo/project-prometheus/releases) page.** If you paid for it, or got it anywhere else, it didn't come from us. See [Legal](#legal).

## Download and play (Windows 10 or 11, 64-bit)

1. Download `ProjectPrometheus-Setup-<version>.exe` from the **Releases** page. It is one file holding everything: the app, the emulator cores, fonts and licences. (Test builds: the repository's **Actions** tab, a **CI** run started by hand on a branch, same file.)
2. Run it. It installs for your user only, with no administrator prompt, and adds a Start menu shortcut.
3. Press **O** (or **Y** on a controller) and choose the folder that holds your games: N64 games (`.z64`, `.n64`, `.v64`) and GBA games (`.gba`), side by side if you like, or the same packed in `.zip`, `.7z` or `.rar` archives (no need to unpack them; an archive with several games shows each one). You can also drop a game onto the window, or double-click a game file if you ticked that option in the installer.
4. Pick a game and press **Enter** (or **A**). To have a friend join, they press **Start** on another controller. Press **F1** for every key.

The **mouse** works everywhere: click a game to pick it and double-click to play it, click menu items (the ‹ › arrows change a value), click the shortcuts along the bottom of the library, and right-click during a game for the menu. Menus also move with W/A/S/D or the number pad, for keyboards without working arrow keys.

| Key | What it does |
|---|---|
| Esc, right-click, or Back/Guide on a controller | Quick menu: save states, picture, fill mode, resolution, volume, players, controls |
| C (in the library) | Controls: change any key or controller button |
| N (in the library) | Play online with friends in your group |
| F5 / F7 | Save state / load state (in the slot chosen in the menu) |
| F8 / F9 | Volume down / up |
| F10 | Who's playing: seat controllers and keyboards for up to 4 players |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |
| Ctrl+R | Reset the game |
| F1 | All controls and shortcuts |

The quick menu shows each item's shortcut next to it, and the shortcuts work while the menu is open too.

**Keyboard (one player), by default:** arrows move, X = A, C = B, Z = Z, Enter = Start, A/S = L/R, I/J/K/L = C buttons, T/F/G/H = D-pad, Shift = walk. Two players can share one keyboard: the left half (WASD) and the right half (arrows) each become a controller from "Who's playing".

**Changing the controls:** press **C** in the library (or Menu > Controls). Pick Keyboard, Left half, Right half or Controller, click a control and press the key or button you want. A key that is already in use swaps places, so nothing is left without a key. Changes save straight away; "Reset to defaults" undoes them.

**GBA buttons, by default:** X = A, C = B, A/S = L/R, Enter = Start, Z = Select, arrows (or T/F/G/H) = D-pad. On a controller: A, B, the shoulder buttons, Start, and the D-pad or stick; Select is either trigger. Hold **Space** to speed the game up (4x, or 2x, 6x or 8x: **Speed-up** in the in-game menu; its sound sped up with it; not during link play or an event); change the key on the Controls screen.

**Each game keeps its own controls.** Change them from a game's menu (**Controls**) and they're saved for that game only, coming back whenever you start it. Controls changed from the library are your usual ones, used by every game you haven't set up on its own.

**The cartridge clock** (for GBA games with one: growing berries, daily events) runs on your computer's real time, so time passes while the app is closed, as it did with the cartridge's battery.

**Saves** go into `Documents\Memories\N64` and `Documents\Memories\GBA`, one folder per game (a computer that already had `Documents\N64 Saves` from an earlier version keeps using it), with the last 10 versions kept. To bring a save from another emulator, start the game once, open the library and press **I** to import its `.srm`, `.sra`, `.eep`, `.fla` or `.mpk` file (GBA: `.sav`, `.srm`, `.sa1`, `.fla` or `.eep`). The emulator's original file is only read, never changed.

### Playing online

Online games happen in a private **group**. One person sets the group up once, and everyone in it shares the group's **key file**. Playing a game then takes two things: the key file (once) and the host's **game code** (each game). Nothing changes for anyone who doesn't use online play.

- **Starting a group (one person, once, about 2 minutes):** press **N** in the library (or click **Play online**), then **Set up**. The app walks you through a free Cloudflare account and a Cloudflare key you copy and paste; it installs a small relay on *your* account and doesn't keep that Cloudflare key. It then gives you the group's key file: click **Save key file** and send the `.prometheuskey` file to your friends.
- **Joining a group:** double-click the key file, drop it onto the window, or press N and click **Add key file**. The installer never contains a key.
- **Hosting a game:** anyone in the group can. In the library, click the game once (or move to it with the arrow keys) so it has the red border, press N, then **Host this game**; the card names the game it will open. To host a different game, close Play online, pick that one and press N again (once hosting, **Stop hosting** first). You get a five-character game code like `K7Q2M`; send it to your friends. When everyone shows up, press **Start**. The input delay starts at the suggested value, and during the game each computer adjusts its own to its connection: lower while the connection is steady, higher when it wobbles.
- **Joining a game:** press N, then **Join**, and type or paste the host's code. Everyone needs the same version of Project Prometheus and their own copy of the same game. Any file of it works (`.z64`, `.v64` or `.n64`, zipped or not, any name), but it has to be the same release: the same region and revision, unmodified. The app checks and says what's different.
- The key file is for all of Project Prometheus: GBA link play below uses the same key and the same relay. It's kept once per computer, so they find it without adding it again. Setting the group up again makes a new key, and old copies stop working. **Remove key** forgets it on this computer.

Each computer runs the game itself and only controller presses travel, so everyone keeps their own picture settings, and couch players can join too (two on one computer and two on another). The host's save is used and stays on the host's computer, backed up first; guests' own saves aren't touched. Save states are off while online, the host's menu pauses everyone, and only the host can reset. If someone drops out, the host keeps playing offline. Under about 40 ms of ping plays great, up to 80 ms is fine for most games; a wired connection plays smoother than Wi-Fi. Details: [`relay/README.md`](relay/README.md).

### GBA link play: trading and battling

GBA games don't play in lockstep like N64 games: each player plays their own game on their own computer at full speed, and only what goes over the link cable travels. Nothing waits on the network except the link itself, so the game never slows down or stutters for anyone.

- **Hosting a link:** pick the GBA game in the library, press **N**, then **Host a link**. You get a link code like `K7Q2M`; send it to your friend.
- **Joining:** your friend picks their own GBA game, presses N, then **Join a link** and types the code.
- In Pokémon Ruby, Sapphire, Emerald, FireRed and LeafGreen, walk into a Pokémon Center and go upstairs to the Cable Club to trade or battle, as with a real link cable. FireRed, LeafGreen and Emerald can use the Wireless Adapter instead (**Link: cable / wireless** on the Play online page); both players need the same choice, and the app says so if they differ.
- Both players need the group's key file, as for online play above. The games don't have to be the same: Ruby trades with FireRed, as on real consoles.
- **Two of your own games on this computer:** no key or friend needed: see [Two of your games linked on this computer](#two-of-your-games-linked-on-this-computer-trading-between-them).

### Pokémon: the lost events, restored (a playthrough guide)

Some of the Generation 3 Pokémon games' best content was only ever handed out at events, on e-Reader cards or from GameCube discs: tickets to islands where Mew, Deoxys, Lugia, Ho-Oh, Latias and Latios wait, the Enigma Berry, Jirachi, extra Battle Tower opponents, and a few things that were never distributed outside Japan at all. The events are long over, and emulators usually offer them only through cheat codes.

Project Prometheus plays the part of the event's own equipment instead: the distribution machine at a store or event (Emerald, FireRed, LeafGreen, over the Wireless Adapter), or the e-Reader reading a card and the Colosseum Bonus Disc sending Jirachi (Ruby, Sapphire, over the link cable). Your game receives each gift through its own menus, exactly as players did then. **No cheats, no save editing:** the app never writes to your game's memory or save; your game decides everything, keeps the gift itself, and saves it.

English versions of the games (game codes `BPEE` Emerald, `BPRE` FireRed, `BPGE` LeafGreen, `AXVE` Ruby, `AXPE` Sapphire). Other languages aren't covered yet.

#### Everything, at a glance

| Game | Gift | What it is | Where you collect it | From when |
|---|---|---|---|---|
| **Ruby, Sapphire** | Eon Ticket | Ferry to Southern Island (Latias in Ruby, Latios in Sapphire) | Your Dad, Petalburg City Gym | Load: after the 5th badge. Sail: after the Hall of Fame |
| | Enigma Berry | The event-only berry, with its own picture and tag. Plant it for more | Your Dad, Petalburg City Gym | After the 5th badge |
| | Clock fix | Nintendo's fix for the berry-clock bug: turns on the game's clock-reset screen | The title screen (hold B + SELECT + Left) | After the 5th badge |
| | Rare words | All 33 rare easy chat words ("trendy sayings") | Your easy chat word lists | After the 5th badge |
| | Battle e-Card | One of three new trainers (Mira, Dorian, Selene), Lv. 50 or Lv. 100 teams | Battle Tower: the first battle of a new challenge | After the Hall of Fame (the Battle Tower) |
| | Jirachi | The WISHMKR Jirachi from the Pokémon Colosseum Bonus Disc (OT WISHMKR, ID 20043, Lv. 5) | Straight into your party | After the 5th badge |
| **Emerald** | Aurora Ticket | Ferry to Birth Island (Deoxys) | Deliveryman, upstairs in any Pokémon Center | Receive: once Mystery Gift is on. Sail: after the Hall of Fame |
| | Mystic Ticket | Ferry to Navel Rock (Lugia and Ho-Oh) | Deliveryman | The same |
| | Old Sea Map | Ferry to Faraway Island (Mew) | Deliveryman | The same |
| | Eon Ticket | Ferry to Southern Island (the Lati you didn't see on TV) | Deliveryman | The same |
| | Pichu Egg | A Pichu egg that knows Surf (a Japan-only event) | Deliveryman (needs a free party slot) | Once Mystery Gift is on |
| | Altering Cave | Route 103's cave gets new wild Pokémon: Mareep, Pineco, Houndour, Teddiursa, Aipom, Shuckle, Stantler or Smeargle (or back to Zubat). Japan only, until now | The deliveryman tells you; the cave changes at once | Once Mystery Gift is on |
| | Visiting Trainer | A trainer comes to battle you in Sootopolis City (Mira, Dorian or Selene, Lv. 50) | The Mystery Events House, Sootopolis City | Once you can reach Sootopolis City |
| | Stamp Card | A Wonder Card with seven stamps to collect, one per visit | Your Wonder Card | Once Mystery Gift is on |
| | Battle Card | A Wonder Card that counts link battles against friends who hold it too; win three for a prize | Your Wonder Card; the prize from the deliveryman | Once Mystery Gift is on |
| **FireRed, LeafGreen** | Aurora Ticket | Seagallop ferry to Birth Island (Deoxys) | Deliveryman, upstairs in any Pokémon Center | Sail: once you reach the Sevii Islands |
| | Mystic Ticket | Seagallop ferry to Navel Rock (Lugia and Ho-Oh) | Deliveryman | The same |
| | Altering Cave | Six Island's cave gets new wild Pokémon (the same eight choices) | The deliveryman tells you | Once Mystery Gift is on and Six Island is reached |

And for every game: **two of your games linked on one computer** to trade between them (Jirachi from Ruby into Emerald, for one), and **daily hints** for Mirage Island and the Lottery Corner (Ruby, Sapphire, Emerald). Both are below.

#### Your playthrough, step by step

**Ruby and Sapphire**

1. **Early game:** nothing to do yet. Daily hints already work: if Mirage Island or a lottery win comes up, the app tells you.
2. **5th badge (your Dad, Norman, at the Petalburg City Gym):** join the Mystery Event Club (below). MYSTERY EVENTS appears on your title menu.
3. **Straight away, load:** the **Eon Ticket**, the **Enigma Berry**, the **clock fix**, the **rare words** and **Jirachi** (one event at a time; each one loads in under a minute). Then talk to your **Dad**: he hands over the Eon Ticket's letter and the Enigma Berry. Jirachi is already in your party.
4. **Plant the Enigma Berry** in soft soil and water it: a tree grows over about four days of real time, as other rare berries do.
5. **After the Hall of Fame:** sail to **Southern Island** from the **Lilycove City** harbor with the Eon Ticket. Take the S.S. Tidal to the **Battle Tower** and load a **Battle e-Card** before your next challenge.
6. **Any time:** link to your Emerald, FireRed or LeafGreen on this computer to trade Jirachi, or anything else, across.

**Emerald**

1. **First Pokédex (after your first battle with your rival on Route 103):** switch Mystery Gift on with the questionnaire (below).
2. **Straight away, receive** the **Pichu Egg**, the **Altering Cave** set you'd like, the **Stamp Card** or the **Battle Card**, and collect from the deliveryman upstairs in any Pokémon Center. Your game holds one Wonder Card at a time: collect a gift before receiving the next.
3. **Before the Hall of Fame,** receive the four tickets one at a time (each goes into your Key Items, so all four can be in your bag).
4. **Once you can reach Sootopolis City:** receive a **Visiting Trainer** and invite them with the survey password (below).
5. **After the Hall of Fame:** sail from the **Lilycove City** harbor to Birth Island, Navel Rock, Faraway Island and Southern Island.

**FireRed and LeafGreen**

1. **Pokédex, then the questionnaire** in any Poké Mart: Mystery Gift is on.
2. **Receive** the **Aurora** and **Mystic Tickets** and collect them from the deliveryman.
3. **Once you reach the Sevii Islands:** the **Seagallop ferry** in Vermilion City takes you to Birth Island and Navel Rock. With Six Island reached, receive an **Altering Cave** set.

#### Emerald, FireRed and LeafGreen: Mystery Gift

**Once per save: switch Mystery Gift on.**

1. *Emerald only:* get your Pokédex first.
2. Go into any Poké Mart and read the **questionnaire** on the counter. Answer **Yes**, and fill it in as **LINK TOGETHER WITH ALL** (four words, from the word lists).
3. The clerk is surprised you know the words: **MYSTERY GIFT** now appears on your game's title menu. **Save your game.**

**Receiving a gift.**

1. **Save your game first.** Opening an event restarts the game, and anything unsaved is lost.
2. In Project Prometheus, press **Esc** (or right-click) for the menu, then **Event gifts**. (From the library: pick the game, press **N**, then **Event gifts**.)
3. Choose the gift and press **Open event**. For a gift with choices (the Altering Cave's Pokémon, the Visiting Trainer), pick with **‹ ›** first. The game restarts at its title screen with the Wireless Adapter plugged in, and a banner at the top says what to do.
4. In the game: **MYSTERY GIFT**, then **WONDER CARDS**, then **WIRELESS COMMUNICATION**. The game finds the "event", connects, receives the **Wonder Card** and saves it. The app tells you where to collect the gift.
   - If you already hold another Wonder Card, the game asks whether to throw it away; if its gift wasn't collected yet, it warns you first.
   - Receiving the same card twice is harmless: the game says you already have it.
5. Continue your game and go **upstairs in any Pokémon Center**. The **deliveryman**, at the left end of the counter, hands over the gift: the ticket goes into your **Key Items** pocket, or the egg into your party. For the Altering Cave he tells you which Pokémon have been seen there, and the cave has them from then on.
6. Back in the app, you can close the event (**Event gifts**, **Close event**), or just carry on playing.

**Using a ticket.**

- **Emerald:** after you've entered the **Hall of Fame**, go to the harbor in **Lilycove City** and show the ticket to the sailor.
- **FireRed, LeafGreen:** the **Seagallop ferry** sails from the **Vermilion City** harbor once you've reached the Sevii Islands in the story. Talk to the sailor there with the ticket in your bag.

**Emerald's Visiting Trainer.**

1. Receive a **Visiting Trainer** card (choose Mira, Dorian or Selene). The deliveryman tells you the survey password: **GIVE ME AWESOME TRAINER**.
2. Fill in the **questionnaire** in any Poké Mart with those four words, and save.
3. Open the same Visiting Trainer event again and connect through **MYSTERY GIFT**, **WONDER CARDS**, **WIRELESS COMMUNICATION**: the game says **"A new TRAINER has arrived."**
4. In **Sootopolis City**, go into the house **southeast of the Pokémon Center** (the Mystery Events House). With a trainer visiting, the stairs are open: go down, and the trainer is waiting. Win, lose or draw, the old man upstairs has something to say about it.

**Emerald's Stamp Card.** Receive it once; then each time you open the **Stamp Card** event again and connect, your card gets the next stamp (Treecko, Torchic, Mudkip, Pikachu, Plusle, Minun, Azurill): seven in all. The deliveryman tells you how many are left. It's a keepsake, as the original was: there's no prize for a full card.

**Emerald's Battle Card.** It counts your **link battles** against trainers whose game holds the same Battle Card, and after **three wins** the deliveryman gives you a prize (a Potion, as Game Freak's own card gave: it's the record that counts). Both players receive the Battle Card, then battle in the Cable Club over link play. (Two copies of the same game can't be linked on one computer, as they'd share one save: battle a friend over the relay.)

#### Ruby and Sapphire: Mystery Events (the e-Reader)

Ruby and Sapphire have no Mystery Gift. Their events came from Nintendo e-Reader cards: the e-Reader, plugged into the game with a link cable, read the card and sent it to the game's **Mystery Events** menu. The app plays that e-Reader. Jirachi came the same way, from the Pokémon Colosseum Bonus Disc over a GameCube link cable.

**Once per save: join the Mystery Event Club.**

1. You need your **5th Gym badge** (from your Dad, Norman, at the Petalburg City Gym).
2. In any Pokémon Center, talk to the man who asks about your **profile** (the Mystery Event Club). Choose to give your profile and enter **MYSTERY EVENT IS EXCITING**.
3. He welcomes you as a member: **MYSTERY EVENTS** now appears on your game's title menu. **Save your game.**

**Loading an event** (the same for all six).

1. **Save your game first.**
2. In Project Prometheus: **Esc**, **Event gifts**, choose the event and press **Open event**. (For a Battle e-Card, pick the trainer and level with **‹ ›** first.) The game restarts at its title screen, connected by link cable to the app's e-Reader.
3. In the game: **MYSTERY EVENTS**. When it says **"Press the A Button to load event"**, press **A**. The game loads the event ("Don't remove the Game Link cable"), shows what you received and saves. Press A: the game restarts.

**Then, for each event:**

- **Eon Ticket:** talk to your **Dad at the Petalburg City Gym**. He has a letter for you: the **Eon Ticket** goes into your Key Items pocket. After the **Hall of Fame**, show it at the **Lilycove City** harbor: the ferry takes you to **Southern Island**. It loads once per game. Friends whose games mix records with yours can receive the ticket too, as the original event allowed.
- **Enigma Berry:** the game says *"Obtained a ENIGMA BERRY! Dad has it at PETALBURG GYM."* Talk to your **Dad** and he gives it to you ("I just received a very rare BERRY"). Its tag in your Berries pocket shows its own picture (a dark violet berry with a gold star) and *"A completely enigmatic BERRY. Appears to have the power of stars."* Held in battle, it restores 30 HP when HP falls below half; eaten from the bag, it restores 30 HP. Plant it to grow more. Loading the event again sends another one if you have none left, as rescanning the card did.
- **Clock fix:** the game says the clock can now be reset. At the title screen, hold **B + SELECT + Left** (on the keyboard: **C + Z + ←**). The game shows its own **Reset RTC** screen: confirm with A. It was Nintendo's fix for the bug that stopped berries growing a year after the game started; with the app's real-date clock the bug never comes up, so you'll rarely need it, but it's there, as on a fixed cartridge.
- **Rare words:** all 33 "trendy sayings" (KTHX BYE, YES SIR!, AVANT GARDE, MEGA, 1-HIT KO!, DESTINY, and the rest) join your easy chat word lists, for your profile, Dewford's trends and the Battle Tower greetings. In the original game they came one at a time from cards and from a man in Mauville City; he still teaches them too.
- **Battle e-Card:** the game says a new trainer waits at the **Battle Tower** at the level you picked. Start a **new challenge** at that level (Lv. 50 or Lv. 100): the trainer is your **first** opponent. Each card's trainer battles you once; load the card again for a rematch, or another card for another trainer. A challenge already in progress isn't interrupted: the trainer waits for your next new one (the two levels keep separate streaks). The three trainers: **Mira** (Cool Trainer: Starmie, Snorlax, Gardevoir), **Dorian** (Expert: Machamp, Metagross, Salamence) and **Selene** (Pokémon Ranger: Milotic, Breloom, Skarmory). Their teams are ours, not copies of Nintendo's cards.
- **Jirachi:** the game says *"POKéMON was sent over!"*: **Jirachi** is in your party, holding a Salac or Ganlon Berry, knowing Wish, Confusion and Rest, from **OT WISHMKR, ID 20043**, exactly as the Bonus Disc made it. Like the disc, the app works out its nature and stats from your save (so one save always gets the same Jirachi). Leave a party slot free. One per save: once your Pokédex shows Jirachi caught, the app says so instead of sending another. Emerald, FireRed and LeafGreen never had a Jirachi event in English: trade it over (below).

#### Two of your games linked on this computer (trading between them)

In the menu of a running GBA game: **Link a second game**. Pick the other game with **‹ ›** and press **Enter**. The second game starts beside the first, with its own save from your library, and the two are joined as if by a link cable (or by Wireless Adapters, if both games are set to wireless).

- Both games show side by side. **F3** (or **Controlling** in the menu) switches which game your keys and controller drive, and which one you hear. The label over each game says which is in play.
- Play each game into a Pokémon Center and upstairs to the **Cable Club**, as two players would: then trade, battle or mix records. Switch with F3 to make each game's choices.
- Each game saves its own progress into the library, as usual. **Link a second game** again (it shows "Linked") closes the second game; quitting closes both.
- Two copies of the same game can't be linked this way (they'd share one save). Ruby with Emerald, Sapphire with FireRed, and so on all work, as on real consoles. FireRed and LeafGreen trade with Ruby, Sapphire and Emerald once they have the National Pokédex and the Sevii Islands' Network Machine is fixed, as in the original games.

#### If something doesn't work

- **The title menu has no MYSTERY GIFT / MYSTERY EVENTS:** the questionnaire (or the Mystery Event Club) step above wasn't done in this save, or the game wasn't saved after it. In Emerald, the questionnaire only works once you have the Pokédex; in Ruby and Sapphire, the club only after the 5th badge.
- **"Communication error" or nothing is found:** close Mystery Gift (or Mystery Events) in the game, open the event again from **Event gifts**, and try once more. Events can't run during link play or online play: end those first.
- **The deliveryman isn't there:** he only appears while your game holds a Wonder Card whose gift is waiting. If you threw the card away, receive it again.
- **"Your BAG's KEY ITEMS POCKET is full" / "party is full":** make room (move a key item or a Pokémon to the PC) and try again.
- **"This EVENT can be loaded only once" / "already have that WONDER CARD" / "already has Jirachi caught":** your game already has this gift.
- **No Visiting Trainer arrives:** check the survey reads exactly GIVE ME AWESOME TRAINER and that you saved after filling it in, then connect again with the same Visiting Trainer event open.
- **The Battle e-Card trainer doesn't appear:** start a new challenge at the level you picked; it's only the first battle of a new streak.

#### Daily hints: Mirage Island and the Lottery Corner (Ruby, Sapphire, Emerald)

Two of these games' luck events are almost impossible to plan for: **Mirage Island** (Route 130) appears only on days when a hidden daily number matches one of your party Pokémon, about 1 in 65,000 per Pokémon per day; and the **Lottery Corner** in the Lilycove Department Store pays its Master Ball only when all five digits of the day's number match one of your Pokémon's ID numbers, about 1 in 100,000.

Project Prometheus reads your running game the way a guide would and tells you, once, on the days you'd be lucky:

- *"Mirage Island is out today! Sail to Route 130, west of Pacifidlog Town, with [your Pokémon] in your party."* The island is gone when the day changes, and that Pokémon has to stay in your party.
- *"Mirage Island could be out today: [your Pokémon], in your PC ([box]), brings it out."* The game only looks at your party, but a matching Pokémon sitting in a PC box counts as soon as you move it into your party: the hint names the box, so you can fetch it and sail the same day.
- *"Lucky day at the Lottery Corner: today's number matches N digits of [your Pokémon]'s ID. Draw a ticket at the Lilycove Department Store to win a [prize]."* (2 digits: PP Up; 3: Exp. Share; 4: Max Revive; 5: Master Ball.) Your party and every PC box count. Once you've drawn today's ticket, it stays quiet.

It never changes the odds or your game, and says nothing on ordinary days. Because the game's clock follows your computer's real clock, the daily numbers change every real day, as on a cartridge. Turn the hints off with **Daily hints** in the in-game menu.

#### Not included

- **Emerald's own e-Reader menu (Trainer Hill e-Cards).** Emerald talks to the e-Reader differently from Ruby and Sapphire: it first sends the e-Reader a program, then receives the card over a link mode the GBA emulator doesn't provide. It needs work inside the emulator core itself; it's on the list in [`docs/backlog.md`](docs/backlog.md).
- **Ribbons** from Ruby and Sapphire's e-Reader cards: they were prizes for tournaments and contests people actually entered, so the app doesn't hand them out.
- **Pokémon Channel's Jirachi** (Europe) and other GameCube-only gifts: the WISHMKR Jirachi covers Jirachi for every game.

#### How it works, briefly

- **Emerald, FireRed, LeafGreen:** the app runs a virtual Wonder distributor on gpSP's Wireless Adapter emulation. It speaks Nintendo's wireless protocol and Game Freak's link layer, and serves a Mystery Gift session: a Wonder Card, plus a short script the deliveryman runs, written in the games' own script language and character set. It also hands out stamps and the visiting trainer, as the distribution system did.
- **Ruby, Sapphire:** the app is the second Game Boy on gpSP's link cable emulation, as the e-Reader was. It answers the game's link handshake and player data exchange, then sends one Mystery Event, which the game checks and runs with its own commands (the berry, the clock reset, the words, the trainer, the Pokémon).
- **Two games here:** each game runs in its own copy of the GBA core, and their link messages go straight from one to the other in memory.
- **Daily hints:** the app reads the game's own numbers (the daily Mirage and lottery values, your Pokémon's IDs) and applies the game's own rules to them.
- All of it was written from the [pret](https://github.com/pret) decompilations of the games, which document what the games expect (and, for Jirachi, from [PKHeX](https://github.com/kwsch/PKHeX)'s documentation of how the Bonus Disc made it), and tested against the games themselves. The details, and what has been checked, are in [decision 0008](docs/decisions/0008-event-gifts.md).

### GBA pictures for big screens

| Look | |
|---|---|
| Classic (default) | Edges gently smoothed while the game's own pixels and text stay as drawn: xBRZ at 3x, the way VBA-M usually shows it, with a light, halo-free sharpening |
| Smooth | Fully smooth, as if redrawn in HD: xBRZ as large as your screen allows. Rounder edges, and small text softens with it |
| Clean | Crisp, with jagged staircase edges rounded off |
| Sharp | Crisp pixels, every one the same size at any screen size, in the game's own colours |
| Handheld | The GBA's own screen: its muted colours, pixel grid and the slight ghosting some games relied on for transparency |
| Pixel-perfect | Every pixel exactly the same whole number of screen pixels (6x on a 1080p screen, 9x on 1440p), as other GBA emulators size their window: nothing blended at all, with a little more border |

### Backing up to Google Drive

Press **B** in the library (or click **Back up**), then **Connect Google Drive** and sign in in your browser. Your saves go into **Memories › N64** and **Memories › GBA** in your Google Drive, quietly, when you close Project Prometheus and every hour while it's open. Only saves go up, never games. Project Prometheus can only see those folders, nothing else in your Drive.

- **Carry on on another computer:** connect the same Google account there, and your in-game saves come down by themselves. Save states are backed up too, but they stay with the computer that made them.
- **Both ways, every time you play:** when you start a game, the app checks Google Drive for that game in the background. If another computer has played since, its save comes down while the game boots, and the game starts again with it on its first screens (only if this computer hasn't saved since). If it arrives later, the running game is left alone and the newer save comes down when you quit.
- **Saved on two computers in between?** The Backup screen asks which one to keep, and the other stays in history. Nothing is ever overwritten without asking.
- **Offline?** Saves stay on the computer and go up when the connection returns.
- **Move or rename the folders** in Drive if you like: backups keep going to them, and other computers find them too. Only if they're deleted does the app start new ones. An "N64 Saves" folder from an earlier version is moved into place as Memories › N64, history included.
- The library shows when the last backup ran. **Back up now** runs one straight away, **Disconnect** signs out and leaves the files in Drive, and both automatic backups and save states can be switched off.

### Picture presets (N64)

| Preset | Look |
|---|---|
| Clean (default) | Rendered at exactly your screen's resolution (no blurry rescaling), HD textures, smooth edges |
| Sharp | Clean plus extra sharpening |
| Retro | 2x resolution with soft scanlines |
| Native | Pixel-exact, original resolution and dithering, like the console |

**Resolution** (in the menu, per game): **Auto** draws the game at exactly your screen's resolution. **Super** draws it at twice that and shrinks it by half, for the smoothest edges; it needs a strong graphics card. Fixed 1x to 8x are there too.

**Textures** (in the menu, per game): **HD (crisp)** redraws the game's small textures at 4x with clean edges (xBRZ, the default), **HD (soft)** is gentler (HQ4X), **Original** keeps them as the console drew them.

Fill modes: **Fit**, **Smart zoom** (fills more of a wide screen by trimming a little top and bottom), **Widescreen** (true 16:9: the 3D world is widened, but games that draw their text and fades for a 4:3 screen show them cut or misplaced, so it's not offered for those; Harvest Moon 64 is one), **Ambient** (a soft glow of the picture fills the side bars) and **Stretch**.

## Documents

- **What to build:** [`docs/BRIEF.md`](docs/BRIEF.md), the single source of truth
- **Decisions:** [`docs/decisions/`](docs/decisions/)
  - [0001 Core, hosting approach and stack](docs/decisions/0001-core-and-stack.md)
  - [0002 Licensing](docs/decisions/0002-licensing.md) (proposed)
  - [0003 Save formats and the canonical master copy](docs/decisions/0003-save-formats.md)
  - [0004 How the player is built](docs/decisions/0004-player-architecture.md)
  - [0005 Online play](docs/decisions/0005-online-play.md)
  - [0006 Google Drive backup](docs/decisions/0006-drive-backup.md)
  - [0007 GBA games](docs/decisions/0007-gba.md)
  - [0008 Pokémon event gifts and daily hints](docs/decisions/0008-event-gifts.md)
- **Setting up Google Drive backup (once, for the project):** [`docs/google-drive-setup.md`](docs/google-drive-setup.md)
- **Privacy policy and terms of use** (shared by every Wolfsbane app): [wolfsbanecompany.com/apps/privacy](https://wolfsbanecompany.com/apps/privacy/) and [/apps/terms](https://wolfsbanecompany.com/apps/terms/), from the website repository [`WolfsbaneCo/wolfsbane-company`](https://github.com/WolfsbaneCo/wolfsbane-company)
- **To build next, with the plan for each:** [`docs/backlog.md`](docs/backlog.md)
- **Waiting on the owner:** [`docs/open-questions.md`](docs/open-questions.md)
- **Unverified assumptions:** [`docs/verify.md`](docs/verify.md)

## Status

| Phase | State |
|---|---|
| 1: Player | **Playable.** Library (with zip, 7z and rar), picture presets, fill modes, resolution up to supersampling, HD textures, fullscreen, quick menu, mouse, save states, local multiplayer up to 4, rumble, button remapping, Windows installer. Still to come: the Vulkan renderer (ParaLLEl-RDP) and per-person profiles |
| 2: Saves | **Working.** In-game saves stored in the library with history; imports from other emulators; `n64save` tool and converters |
| 3: Google Drive sync | **Working.** Connect in the app, automatic backups (on close, hourly, at start), carrying on across computers with in-game saves, choosing when two computers both saved, offline retries, save states as a per-computer backup. Checked against a stand-in for Google with two Linux computers and a Windows one; waiting for the project's Google registration ([setup](docs/google-drive-setup.md)). Still to come: syncing settings, device-to-device sharing on a home network, optional encryption |
| 4: Netplay | **Working.** Private groups with a shared key file and a relay on the group owner's free Cloudflare account (set up from inside the app), game codes, lockstep with game and version checks, an input delay that adjusts itself during the game, the host's save. Checked with Harvest Moon 64 and Super Smash Bros. between two copies on Linux and between Linux and Windows; not yet over the internet |
| GBA | **Working.** gpSP core picked by the game's header, five looks for big screens, saves in the library with imports and Drive backup, link play over the relay (link cable and Wireless Adapter) and between two games on one computer, Pokémon events restored for Emerald, FireRed, LeafGreen, Ruby and Sapphire (tickets, Enigma Berry, clock fix, rare words, Battle e-Cards, Jirachi, Pichu, Altering Cave, Visiting Trainer, Stamp and Battle Cards), daily hints. Still to be tried in real play: a trade over the internet, the island trips, a Battle e-Card and Visiting Trainer battle. Not built: Emerald's e-Reader menu (Trainer Hill) |
| 5: Cartridge | Not started |

## Building from source

Needs CMake 3.20+ and a C++20 compiler. SDL3 is downloaded and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The N64 core is a separate download from its own project, built at a pinned commit by `tools/build-core-windows.sh` (MinGW-w64 on Linux). Put `mupen64plus_next_libretro.dll` in a `cores` folder next to `ProjectPrometheus.exe`. The GBA core (gpSP, also pinned) is built with the app by GCC or Clang and put in `cores` automatically; with Microsoft's compiler, build it apart with `tools/build-gba-core-windows.sh` (MinGW-w64). CI does all of this and builds the installer with Inno Setup (`installer/project-prometheus.iss`). CI runs only when started by hand (Actions > CI > Run workflow) or by a version tag, never on ordinary pushes, so the account's Actions minutes go to final builds; run `tools/check-before-push.sh` locally before pushing. To publish a release, push a tag like `v0.1.0`, or run the CI workflow by hand on `main` (Actions > CI > Run workflow): it tags `main` with the version in `CMakeLists.txt` and attaches the installer.

On Linux, SDL needs the X11/Wayland, ALSA/PulseAudio and udev development packages, and online play needs OpenSSL's (see `.github/workflows/ci.yml`). libcurl is downloaded and built automatically; on Windows it uses the system's own TLS. The online relay lives in [`relay/`](relay/README.md).

### `n64save`: save tools for the command line

```sh
n64save inspect "<folder with saves>"                       # describe saves; strictly read-only
n64save formats                                            # list the save formats it converts
n64save convert --from canonical --to retroarch-mupen64plus-next --name "<game>" \
  --out ./out "N64 Saves/<GAME>/current/canonical/sram.bin"
n64save rom-id "<game file>"                               # identify a game by header and checksum
```

## Ground rules

Game files, saves, save states and credentials never go into this repository (see `.gitignore`; CI checks it). Tests use synthetic data or public-domain homebrew only.

## Legal

- **Free, always.** Project Prometheus is free of charge. No one is authorised to sell it or charge for it: no paid downloads, paid bundles, paywalls, subscriptions or "donation required" downloads. The only official downloads are on this repository's [Releases](https://github.com/WolfsbaneCo/project-prometheus/releases) page.
- **The name and logo are reserved.** "Project Prometheus", "Prometheus 64" and the flame logo are trademarks of Wolfsbane Co. The code's licence doesn't cover them ([`TRADEMARKS.md`](TRADEMARKS.md)).
  - Sharing the unchanged official installer for free is fine.
  - Modified versions must use their own name and icons, and must not suggest they come from us.
- **No games, no BIOS.** The app contains no games, BIOS or system files of any console maker, and never downloads, finds or shares them. Use it only with games you own and copied yourself, as the law where you live allows. Don't ask for, post or link to game files in issues or discussions: they will be deleted.
- **Not affiliated.** Project Prometheus is not affiliated with, endorsed or sponsored by Nintendo, The Pokémon Company, Game Freak, Google or Cloudflare. Their names and trademarks belong to them and appear here only to say what the app works with.
- **No warranty.** The app is provided "as is", without warranty of any kind, and the authors aren't liable for any damage from its use, including lost saves (GNU GPL version 3, sections 15 and 16). Keep backups of saves that matter.
- **Your accounts, your data.** Online play runs through a relay on the group owner's own Cloudflare account, and backups go to your own Google Drive. Wolfsbane Co. receives none of your games, saves or data. Each player is responsible for following the terms of the services they connect. Read the [privacy policy](https://wolfsbanecompany.com/apps/privacy/) and [terms](https://wolfsbanecompany.com/apps/terms/).
- **Licences.** The app's code is under the [GNU General Public License, version 3 or later](LICENSE): anyone may share and change it, and copies and changed versions must stay under the same licence, with their source. Redistributions must keep the [`NOTICE`](NOTICE) file, whose additional terms reserve the name and logo. The emulator cores (GPL-2.0) and other components keep their own licences, listed with where to get their source in [`installer/THIRD-PARTY-NOTICES.txt`](installer/THIRD-PARTY-NOTICES.txt). The installer asks you to accept these terms ([`installer/TERMS.txt`](installer/TERMS.txt)) and installs every licence in its `licenses` folder.
- **Reporting.** To report a copy being sold, a copyright concern or a takedown request, open an issue on this repository.
