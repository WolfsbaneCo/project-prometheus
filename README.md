# Project Prometheus 64

**Prometheus 64** is a modern desktop player for N64 games, part of **Project Prometheus**: keeping classic games playable, and their saves alive, instead of letting them fade into oblivion. It is built on a proven emulator core and adds its own sharper picture, simpler controls, couch multiplayer and a save library that keeps every save safe.

The app plays games the user already owns and adds themselves. It never downloads, finds or distributes games (brief section 2).

## Download and play (Windows 10 or 11, 64-bit)

1. Download `Prometheus64-Setup-<version>.exe` from the **Releases** page. It is one file holding everything: the app, the emulator core, fonts and licences. (Test builds: the repository's **Actions** tab, latest green **CI** run, same file.)
2. Run it. It installs for your user only, with no administrator prompt, and adds a Start menu shortcut.
3. Press **O** (or **Y** on a controller) and choose the folder that holds your games: `.z64`, `.n64` and `.v64` files, or the same packed in `.zip`, `.7z` or `.rar` archives (no need to unpack them; an archive with several games shows each one). You can also drop a game onto the window, or double-click a game file if you ticked that option in the installer.
4. Pick a game and press **Enter** (or **A**). To have a friend join, they press **Start** on another controller. Press **F1** for every key.

The **mouse** works everywhere: click a game to play it, click menu items (the ‹ › arrows change a value), click the shortcuts along the bottom of the library, and right-click during a game for the menu. Menus also move with W/A/S/D or the number pad, for keyboards without working arrow keys.

| Key | What it does |
|---|---|
| Esc, right-click, or Back/Guide on a controller | Quick menu: save states, picture, fill mode, resolution, volume, players, controls |
| C (in the library) | Controls: change any key or controller button |
| F2 / F3 | Next fill mode / next picture preset |
| F5 / F7 / F6 | Save state / load state / next state slot |
| F10 | Who's playing: seat controllers and keyboards for up to 4 players |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |

**Keyboard (one player), by default:** arrows move, X = A, C = B, Z = Z, Enter = Start, A/S = L/R, I/J/K/L = C buttons, T/F/G/H = D-pad, Shift = walk. Two players can share one keyboard: the left half (WASD) and the right half (arrows) each become a controller from "Who's playing".

**Changing the controls:** press **C** in the library (or Menu > Controls). Pick Keyboard, Left half, Right half or Controller, click a control and press the key or button you want. A key that is already in use swaps places, so nothing is left without a key. Changes save straight away; "Reset to defaults" undoes them.

**Saves** go into `Documents\N64 Saves`, one folder per game, with the last 10 versions kept. To bring a save from another emulator, start the game once, open the library and press **I** to import its `.srm`, `.sra`, `.eep`, `.fla` or `.mpk` file. The emulator's original file is only read, never changed.

### Picture presets

| Preset | Look |
|---|---|
| Clean (default) | High resolution matched to your screen, smooth edges |
| Sharp | Clean plus extra sharpening |
| Retro | 2x resolution with soft scanlines |
| Native | Pixel-exact, original resolution and dithering, like the console |

Fill modes: **Fit**, **Smart zoom** (fills more of a wide screen by trimming a little top and bottom), **Widescreen** (true 16:9 where the game allows it), **Ambient** (a soft glow of the picture fills the side bars) and **Stretch**.

## Documents

- **What to build:** [`docs/BRIEF.md`](docs/BRIEF.md), the single source of truth
- **Decisions:** [`docs/decisions/`](docs/decisions/)
  - [0001 Core, hosting approach and stack](docs/decisions/0001-core-and-stack.md)
  - [0002 Licensing](docs/decisions/0002-licensing.md) (proposed)
  - [0003 Save formats and the canonical master copy](docs/decisions/0003-save-formats.md)
  - [0004 How the player is built](docs/decisions/0004-player-architecture.md)
- **Waiting on the owner:** [`docs/open-questions.md`](docs/open-questions.md)
- **Unverified assumptions:** [`docs/verify.md`](docs/verify.md)

## Status

| Phase | State |
|---|---|
| 1: Player | **Playable.** Library, picture presets, fill modes, internal resolution, fullscreen, quick menu, save states, local multiplayer up to 4, rumble, Windows installer. Still to come: the Vulkan renderer (ParaLLEl-RDP), per-person profiles and button remapping |
| 2: Saves | **Working.** In-game saves stored in the library with history; imports from other emulators; `n64save` tool and converters |
| 3: Google Drive sync | Not started |
| 4: Netplay | Not started |
| 5: Cartridge | Not started |

## Building from source

Needs CMake 3.20+ and a C++20 compiler. SDL3 is downloaded and built automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The emulator core is a separate download from its own project, built at a pinned commit by `tools/build-core-windows.sh` (MinGW-w64 on Linux). Put `mupen64plus_next_libretro.dll` in a `cores` folder next to `Prometheus64.exe`. CI does all of this and builds the installer with Inno Setup (`installer/prometheus64.iss`). Pushing a tag like `v0.1.0` publishes a release.

On Linux, SDL needs the X11/Wayland, ALSA/PulseAudio and udev development packages (see `.github/workflows/ci.yml`).

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
