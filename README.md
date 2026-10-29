# Project Prometheus 64

**Prometheus 64** is a modern desktop player for N64 games, part of **Project Prometheus**: keeping classic games playable, and their saves alive, instead of letting them fade into oblivion. It is built on a proven emulator core and adds its own sharper picture, simpler controls, couch multiplayer and a save library that keeps every save safe.

The app plays games the user already owns and adds themselves. It never downloads, finds or distributes games (brief section 2).

## Download and play (Windows 10 or 11, 64-bit)

1. Download `Prometheus64-Setup-<version>.exe` from the **Releases** page. It is one file holding everything: the app, the emulator core, fonts and licences. (Test builds: the repository's **Actions** tab, latest green **CI** run, same file.)
2. Run it. It installs for your user only, with no administrator prompt, and adds a Start menu shortcut.
3. Press **O** (or **Y** on a controller) and choose the folder that holds your games: `.z64`, `.n64` and `.v64` files, or the same packed in `.zip`, `.7z` or `.rar` archives (no need to unpack them; an archive with several games shows each one). You can also drop a game onto the window, or double-click a game file if you ticked that option in the installer.
4. Pick a game and press **Enter** (or **A**). To have a friend join, they press **Start** on another controller. Press **F1** for every key.

The **mouse** works everywhere: click a game to pick it and double-click to play it, click menu items (the ‹ › arrows change a value), click the shortcuts along the bottom of the library, and right-click during a game for the menu. Menus also move with W/A/S/D or the number pad, for keyboards without working arrow keys.

| Key | What it does |
|---|---|
| Esc, right-click, or Back/Guide on a controller | Quick menu: save states, picture, fill mode, resolution, volume, players, controls |
| C (in the library) | Controls: change any key or controller button |
| N (in the library) | Play online with friends in your group |
| F2 / F3 | Next fill mode / next picture preset |
| F5 / F7 / F6 | Save state / load state / next state slot |
| F10 | Who's playing: seat controllers and keyboards for up to 4 players |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

**Keyboard (one player), by default:** arrows move, X = A, C = B, Z = Z, Enter = Start, A/S = L/R, I/J/K/L = C buttons, T/F/G/H = D-pad, Shift = walk. Two players can share one keyboard: the left half (WASD) and the right half (arrows) each become a controller from "Who's playing".

**Changing the controls:** press **C** in the library (or Menu > Controls). Pick Keyboard, Left half, Right half or Controller, click a control and press the key or button you want. A key that is already in use swaps places, so nothing is left without a key. Changes save straight away; "Reset to defaults" undoes them.

**Saves** go into `Documents\Memories\N64`, one folder per game (a computer that already had `Documents\N64 Saves` from an earlier version keeps using it), with the last 10 versions kept. To bring a save from another emulator, start the game once, open the library and press **I** to import its `.srm`, `.sra`, `.eep`, `.fla` or `.mpk` file. The emulator's original file is only read, never changed.

### Playing online

Online games happen in a private **group**. One person sets the group up once, and everyone in it shares the group's **key file**. Playing a game then takes two things: the key file (once) and the host's **game code** (each game). Nothing changes for anyone who doesn't use online play.

- **Starting a group (one person, once, about 2 minutes):** press **N** in the library (or click **Play online**), then **Set up**. The app walks you through a free Cloudflare account and a Cloudflare key you copy and paste; it installs a small relay on *your* account and doesn't keep that Cloudflare key. It then gives you the group's key file: click **Save key file** and send the `.prometheuskey` file to your friends.
- **Joining a group:** double-click the key file, drop it onto the window, or press N and click **Add key file**. The installer never contains a key.
- **Hosting a game:** anyone in the group can. In the library, click the game once (or move to it with the arrow keys) so it has the red border, press N, then **Host this game**; the card names the game it will open. To host a different game, close Play online, pick that one and press N again (once hosting, **Stop hosting** first). You get a five-character game code like `K7Q2M`; send it to your friends. When everyone shows up, press **Start**. The input delay starts at the suggested value, and during the game each computer adjusts its own to its connection: lower while the connection is steady, higher when it wobbles.
- **Joining a game:** press N, then **Join**, and type or paste the host's code. Everyone needs the same version of Prometheus 64 and their own copy of the same game. Any file of it works (`.z64`, `.v64` or `.n64`, zipped or not, any name), but it has to be the same release: the same region and revision, unmodified. The app checks and says what's different.
- The key file is for all of Project Prometheus, not only this app: future Project Prometheus apps with online features (trading in a GBA app, say) use the same key and the same relay. It's kept once per computer, so they find it without adding it again. Setting the group up again makes a new key, and old copies stop working. **Remove key** forgets it on this computer.

Each computer runs the game itself and only controller presses travel, so everyone keeps their own picture settings, and couch players can join too (two on one computer and two on another). The host's save is used and stays on the host's computer, backed up first; guests' own saves aren't touched. Save states are off while online, the host's menu pauses everyone, and only the host can reset. If someone drops out, the host keeps playing offline. Under about 40 ms of ping plays great, up to 80 ms is fine for most games; a wired connection plays smoother than Wi-Fi. Details: [`relay/README.md`](relay/README.md).

### Backing up to Google Drive

Press **B** in the library (or click **Back up**), then **Connect Google Drive** and sign in in your browser. Your saves go into **Memories › N64** in your Google Drive, quietly, when you close Prometheus 64 and every hour while it's open. Only saves go up, never games. Prometheus 64 can only see that folder, nothing else in your Drive.

- **Carry on on another computer:** connect the same Google account there, and your in-game saves come down by themselves. Save states are backed up too, but they stay with the computer that made them.
- **Saved on two computers in between?** The Backup screen asks which one to keep, and the other stays in history. Nothing is ever overwritten without asking.
- **Offline?** Saves stay on the computer and go up when the connection returns.
- **Move or rename the folders** in Drive if you like: backups keep going to them, and other computers find them too. Only if they're deleted does the app start new ones. An "N64 Saves" folder from an earlier version is moved into place as Memories › N64, history included.
- The library shows when the last backup ran. **Back up now** runs one straight away, **Disconnect** signs out and leaves the files in Drive, and both automatic backups and save states can be switched off.

### Picture presets

| Preset | Look |
|---|---|
| Clean (default) | Rendered at exactly your screen's resolution (no blurry rescaling), HD textures, smooth edges |
| Sharp | Clean plus extra sharpening |
| Retro | 2x resolution with soft scanlines |
| Native | Pixel-exact, original resolution and dithering, like the console |

**Resolution** (in the menu, per game): **Auto** draws the game at exactly your screen's resolution. **Super** draws it at twice that and shrinks it by half, for the smoothest edges; it needs a strong graphics card. Fixed 1x to 8x are there too.

**Textures** (in the menu, per game): **HD (crisp)** redraws the game's small textures at 4x with clean edges (xBRZ, the default), **HD (soft)** is gentler (HQ4X), **Original** keeps them as the console drew them.

Fill modes: **Fit**, **Smart zoom** (fills more of a wide screen by trimming a little top and bottom), **Widescreen** (true 16:9 where the game allows it: Harvest Moon 64 shows extra scenery at both sides), **Ambient** (a soft glow of the picture fills the side bars) and **Stretch**.

## Documents

- **What to build:** [`docs/BRIEF.md`](docs/BRIEF.md), the single source of truth
- **Decisions:** [`docs/decisions/`](docs/decisions/)
  - [0001 Core, hosting approach and stack](docs/decisions/0001-core-and-stack.md)
  - [0002 Licensing](docs/decisions/0002-licensing.md) (proposed)
  - [0003 Save formats and the canonical master copy](docs/decisions/0003-save-formats.md)
  - [0004 How the player is built](docs/decisions/0004-player-architecture.md)
  - [0005 Online play](docs/decisions/0005-online-play.md)
  - [0006 Google Drive backup](docs/decisions/0006-drive-backup.md)
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
| 5: Cartridge | Not started |

## Building from source

Needs CMake 3.20+ and a C++20 compiler. SDL3 is downloaded and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The emulator core is a separate download from its own project, built at a pinned commit by `tools/build-core-windows.sh` (MinGW-w64 on Linux). Put `mupen64plus_next_libretro.dll` in a `cores` folder next to `Prometheus64.exe`. CI does all of this and builds the installer with Inno Setup (`installer/prometheus64.iss`). To publish a release, push a tag like `v0.1.0`, or run the CI workflow by hand on `main` (Actions > CI > Run workflow): it tags `main` with the version in `CMakeLists.txt` and attaches the installer.

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
