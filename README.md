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

**GBA buttons, by default:** X = A, C = B, A/S = L/R, Enter = Start, Z = Select, arrows (or T/F/G/H) = D-pad. On a controller: A, B, the shoulder buttons, Start, and the D-pad or stick; Select is either trigger. Hold **Space** to speed the game up (4x, without sound; not during link play or an event); change the key on the Controls screen.

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

### GBA pictures for big screens

| Look | |
|---|---|
| Smooth (default) | Smooth, as if redrawn in HD, while text, eyes and small details keep the shapes the game drew |
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
- **Setting up Google Drive backup (once, for the project):** [`docs/google-drive-setup.md`](docs/google-drive-setup.md)
- **Privacy policy and terms of use** (shared by every Wolfsbane app): [wolfsbanecompany.com/apps/privacy](https://wolfsbanecompany.com/apps/privacy/) and [/apps/terms](https://wolfsbanecompany.com/apps/terms/), from the website repository [`WolfsbaneCo/wolfsbane-company`](https://github.com/WolfsbaneCo/wolfsbane-company)
- **Waiting on the owner:** [`docs/open-questions.md`](docs/open-questions.md)
- **Unverified assumptions:** [`docs/verify.md`](docs/verify.md)

## Status

| Phase | State |
|---|---|
| 1: Player | **Playable.** Library (with zip, 7z and rar), picture presets, fill modes, resolution up to supersampling, HD textures, fullscreen, quick menu, mouse, save states, local multiplayer up to 4, rumble, button remapping, Windows installer. Still to come: the Vulkan renderer (ParaLLEl-RDP) and per-person profiles |
| 2: Saves | **Working.** In-game saves stored in the library with history; imports from other emulators; `n64save` tool and converters |
| 3: Google Drive sync | **Working.** Connect in the app, automatic backups (on close, hourly, at start), carrying on across computers with in-game saves, choosing when two computers both saved, offline retries, save states as a per-computer backup. Checked against a stand-in for Google with two Linux computers and a Windows one; waiting for the project's Google registration ([setup](docs/google-drive-setup.md)). Still to come: syncing settings, device-to-device sharing on a home network, optional encryption |
| 4: Netplay | **Working.** Private groups with a shared key file and a relay on the group owner's free Cloudflare account (set up from inside the app), game codes, lockstep with game and version checks, an input delay that adjusts itself during the game, the host's save. Checked with Harvest Moon 64 and Super Smash Bros. between two copies on Linux and between Linux and Windows; not yet over the internet |
| GBA | **Working.** gpSP core picked by the game's header, five looks for big screens, saves in the library with imports and Drive backup, link play over the relay (link cable and Wireless Adapter). A real trade over the internet is still to be tried |
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
- **No warranty.** The app is provided "as is", without warranty of any kind, and the authors aren't liable for any damage from its use, including lost saves (Apache License 2.0, sections 7 and 8). Keep backups of saves that matter.
- **Your accounts, your data.** Online play runs through a relay on the group owner's own Cloudflare account, and backups go to your own Google Drive. Wolfsbane Co. receives none of your games, saves or data. Each player is responsible for following the terms of the services they connect. Read the [privacy policy](https://wolfsbanecompany.com/apps/privacy/) and [terms](https://wolfsbanecompany.com/apps/terms/).
- **Licences.** The app's code is under the [Apache License 2.0](LICENSE); redistributions must keep the [`NOTICE`](NOTICE) file. The emulator cores (GPL-2.0) and other components keep their own licences, listed with where to get their source in [`installer/THIRD-PARTY-NOTICES.txt`](installer/THIRD-PARTY-NOTICES.txt). The installer asks you to accept these terms ([`installer/TERMS.txt`](installer/TERMS.txt)) and installs every licence in its `licenses` folder.
- **Reporting.** To report a copy being sold, a copyright concern or a takedown request, open an issue on this repository.
