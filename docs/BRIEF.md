# Project Prometheus 64: Project Brief

**Prometheus 64** is a clean, modern desktop N64 player, part of **Project Prometheus**: keeping classic games playable and their saves alive. It plays games the user already owns. It wraps a proven emulator core with a polished interface, high-end graphics, flexible controls, portable saves, optional Google Drive backup and simple online play with friends and family.

This document is the single source of truth for what to build. Read all of it before writing code. Items marked **VERIFY** are things the author believes are true but did not confirm; check them against current documentation before relying on them. Items under **Open questions** must be answered by the owner, so ask them instead of guessing.

> Status of VERIFY items and open questions is tracked in [`docs/verify.md`](verify.md) and
> [`docs/open-questions.md`](open-questions.md). Decisions are in [`docs/decisions/`](decisions/).

---

## 1. Goals

1. **Clean, smooth picture.** Sharp geometry, no dither grain, no blurry stretching, no glitches. It should feel like a remaster of the original look without changing the game.
2. **Perfect fullscreen.** Fills the screen without distorting the 4:3 picture, with several fill modes and a quick hotkey to switch.
3. **A beautiful, simple interface.** Dark, minimal, console-like. No 2000s-era menus.
4. **Portable saves.** In-game saves work across this app, RetroArch, newer Project64 and (optionally later) a real cartridge.
5. **Optional Google Drive backup.** Local first, Drive on top. Works with no sign-in at all.
6. **Easy local multiplayer.** Each player picks their own device and keeps their own control profile.
7. **Simple online play** with a trusted friend or partner (later phase).

## 2. Non-goals

- Writing a new N64 emulator core from scratch. Use an existing, proven core.
- Downloading, finding, sharing or distributing games. The app only loads files the owner adds themselves.
- Uploading game files (ROMs) to any cloud service.
- Transfer Pak and Game Boy cartridge support (possible far future, out of scope).
- Rollback netplay (not practical for N64; see section 11).
- A browser version. This is a native desktop app.

## 3. Decisions already made

| Topic | Decision |
|---|---|
| Form | Native desktop program, not a browser app. Windows first. |
| Core | Reuse an existing core. ParaLLEl-RDP (Vulkan) preferred for accuracy. |
| Why not Jabo | The owner's current Project64 1.6 uses the Jabo Direct3D8 plugin, which causes the glitches and dated look. |
| Saves | Local files are always the real copy. Drive is optional. |
| Save formats | Keep several formats if needed for portability (section 8). |
| Drive sync | On emulator close and every 1 hour while running. Keep last 10 in-game saves and last 10 save states per game. |
| ROM storage | Local folder per device. Never Google Drive. Storage location is a setting so other backends can be added later. |
| Netplay | Lockstep input-delay netplay. Save states disabled while connected. Host's in-game save is used. |
| Cartridge | Optional module, built last. Design the save format so it is possible. |

## 4. Architecture

### 4.1 Recommended approach

A native app that **hosts an existing emulator core** and adds its own window, renderer, shader chain, menu, input system and save library.

Evaluate these options in a short spike (about one day) and recommend one in a short decision note in `docs/decisions/`:

- **A. libretro host.** Load a libretro N64 core (for example Mupen64Plus-Next or ParaLLEl N64) as a plug-in library. Pros: well-defined API, saves/inputs/video are handled through callbacks, same ecosystem as RetroArch. Cons: some core options are exposed only as libretro variables.
- **B. Embed Mupen64Plus directly** with the ParaLLEl-RDP (Vulkan) plugin, as projects like simple64 and RMG do. Pros: direct control of video, input and netplay hooks. Cons: more integration work.
- **C. Wrap an existing emulator** (for example current Project64 or RMG) with a launcher. Pros: least work. Cons: the interface and fullscreen behaviour are limited to what that app exposes.

Decision criteria: ability to control scaling and shaders ourselves, accuracy (glitch-free), netplay feasibility, input flexibility, licensing, maintenance burden.

**Licensing: VERIFY.** Many N64 cores and plugins are GPL. Work out what that means for how this app links to or loads them, including what changes when the app is distributed, and document the decision.

### 4.2 Suggested layers

- **Core host:** loads the core, runs the emulation loop, exposes save memory and state.
- **Renderer:** takes the core's frame, applies upscaling, filters and the shader chain, and presents it with correct scaling.
- **Input service:** device discovery, per-player profiles, hotplug.
- **Save service:** the library, converters, backups, history (section 8).
- **Sync service:** Google Drive, local network pairing (sections 9 and 10).
- **Netplay service:** lobby, relay, lockstep (section 11).
- **UI:** the interface shell (section 6).

Keep each layer behind a clear interface so cores, storage backends and sync backends can be swapped.

### 4.3 Stack

Not decided. Pick based on the spike. Whatever is chosen, the interface must use a modern GPU-accelerated UI and the app must not depend on a browser. A native shell with a web-technology UI layer (for example Tauri) or a native UI toolkit (for example Qt) are both acceptable. Explain the choice in the decision note.

## 5. Graphics and display

### 5.1 Quality techniques

All of these should be controllable per game and ship as presets.

- **Internal resolution:** 1x to 8x of native, with a sensible automatic choice for the owner's screen.
- **VI (video interface) handling:** options to disable the N64's dither pattern and its built-in blur, and to control divot and gamma handling. **VERIFY** the exact options exposed by the chosen renderer.
- **Anti-aliasing:** a post-process AA stage (SMAA or similar) on top of any hardware AA the core offers.
- **Texture filtering:** options for the original 3-point filter and for smooth bilinear.
- **Sharpening:** a gentle, optional sharpen pass after downscaling.
- **Downscale quality:** when rendering above screen resolution, downsample with a high-quality filter (for example Lanczos), never nearest-neighbour or plain bilinear.
- **Texture packs:** support loading replacement texture packs per game (a hi-res pack folder per game). This is the main route to genuine added detail. Check whether one exists for each game the owner plays.
- **Color:** correct gamma, optional HDR output if the display supports it.
- **Optional retro shaders:** CRT, scanlines, bloom, as a creative option, off by default.

Presets to ship: **Clean** (default), **Sharp**, **Retro**, **Native (accuracy reference)**.

Expectation to keep honest in the UI and docs: resolution and filtering clean up geometry and edges; fine texture detail is limited by the original art unless a texture pack is used.

### 5.2 Fullscreen and scaling

Requirements:

- Borderless fullscreen window (instant alt-tab, no flicker), output at the display's native resolution and refresh rate.
- Smooth frame pacing, VSync handling, and refresh-rate matching.
- Handle multi-monitor and OS display scaling correctly.
- Hide the cursor, remember window mode, and allow a hotkey to toggle fullscreen and to cycle fill modes.

Fill modes (switchable live, remembered per game):

1. **Fit (4:3).** Full picture, no distortion, bars on the sides.
2. **Smart zoom.** A slight crop that fills more of the screen while keeping proportions.
3. **Widescreen.** Per-game patches where a reliable patch exists. Mark unsupported games clearly.
4. **Ambient fill.** A softly blurred, darkened copy of the picture in the side bars instead of black.
5. **Stretch.** Available but not default, with a visible warning that it distorts.

Also provide an **overscan crop** per game (many games have a thin black border).

Scaling approach: render at a high internal resolution, then downscale (or integer upscale) to the display. Reference numbers, assuming a 240 line NTSC picture and a 288 line PAL picture:

| Display height | NTSC (240) | PAL (288) |
|---|---|---|
| 1080 | 4.5x (render 5x and downscale) | 3.75x |
| 1440 | 6x exactly | 5x exactly |
| 2160 | 9x exactly | 7.5x |

Where the multiple is exact, scale cleanly. Where it is not, render one step higher and downscale with the high-quality filter.

## 6. User interface

- Dark, minimal, console-like. Large game tiles with cover art, smooth transitions, controller and keyboard navigation.
- Game metadata comes from the game's built-in header and, optionally, a local cache of names and cover art. Do not bundle copyrighted art; let the user add or fetch images in a way that respects licensing. **Open question.**
- **Quick menu overlay** during play: save/load state, reset, fill mode, preset, volume, controller assignment.
- **Save state browser** with thumbnails, dates and the 10-version history.
- **Per-game profile screen:** resolution, preset, fill mode, overscan crop, widescreen, texture pack, controls.
- **Console options** (to mirror the real hardware):
  - 4 controller ports with remapping
  - Controller Pak management (view, copy, delete notes)
  - Rumble Pak
  - Expansion Pak on or off (some games require it)
  - Region and refresh rate (NTSC or PAL)
  - Pause, reset, power cycle
- A first-run setup that detects the display and GPU and picks sensible defaults.

## 7. Input and local multiplayer

- **Per-person profiles.** Named profiles (for example the owner and their wife) with their own layout and sensitivity. Profiles sync with settings (section 9.5).
- **Press to join.** On a "who is playing" screen, each player presses any key or button on the device they want; the app assigns the next player slot.
- **Mix devices freely:** keyboard, gamepads, original N64 controllers via USB adapters.
- **Conflict detection:** warn immediately if two players share a key.
- **Keyboard cautions:** many keyboards cannot register more than a few simultaneous keys. Show a clear warning when two players share a keyboard, and recommend gamepads. A keyboard has no analog stick, so provide smoothing and a modifier for walking-speed movement, but state plainly that stick-rotation minigames are much easier on a gamepad.
- Fully remappable, including C buttons, Z trigger and analog stick, with per-game overrides, hotplug support and rumble.
- **Controller Pak per person (option).** Each person's virtual Controller Pak follows their profile instead of the player slot. Make it switchable per game.

## 8. Save system

### 8.1 Concepts

- **In-game saves** (the cartridge's own memory: SRAM, EEPROM, FlashRAM, Controller Pak) are the portable ones.
- **Save states** are snapshots of one emulator and one core version. They only load in the same core. Treat them as non-portable.

### 8.2 Storage layout

One clear, visible library folder, organised by game and not by emulator:

```
Memories/
  N64/                  one folder per console (GBA/ later); the same layout in Google Drive
    <Game name>/
      current/          latest master copy, in raw form plus canonical form
      history/          last 10 in-game saves, timestamped, never overwritten
      states/           last 10 save states, tagged with core and version
      info.json         game ID, save type(s), checksums, last played, device
```

- Identify games by the ROM header ID and checksum, never by file name.
- The location of the library is a setting, defaulting to a visible folder under Documents.

### 8.3 Formats and portability

Different emulators store the same data differently: separate files per save type, a single combined file, and sometimes different byte order or padding. **VERIFY** the current formats for each target before writing converters.

- Keep one **master copy** per game: the most recent real save in its **original raw form**, plus a canonical internal form.
- Generate the other formats on demand from the master copy. Targets: current app, **newer Project64**, **RetroArch** (note: the layout depends on the core, so the adapter must know which core is used), and **real cartridge** (phase 5).
- When a target emulator's save changes, detect it, convert it into the master copy, and regenerate the other formats so they stay in step.
- If two formats both changed since the last sync, **never overwrite silently**. Keep both and ask the user.

### 8.4 Safety rules (non-negotiable)

- **Back up before every import, conversion, sync or overwrite.** Backups are timestamped and feed the 10-version history.
- **Round-trip tests for every converter:** convert A to B and back to A, and assert the result is byte-for-byte identical. Run these before any converter touches real saves. Add a CI job for them.
- Never modify the owner's original files in place. Import creates copies.
- Never commit saves, states or game files to git. Add them to `.gitignore`.

### 8.5 Importing the owner's existing saves

The owner has an existing Project64 1.6 install with these items (found by listing the folder; contents not inspected):

- An in-game save file for Harvest Moon 64 (`.sra`, about 4 KB) and ten save states (`.pj`, `.pj0` to `.pj9`, zipped).
- A `Save/` subfolder with further game folders.
- Plugins: Jabo Direct3D6 and Direct3D8 video, Jabo DInput and NRage input, Jabo DSound and Zilmar audio, a standard RSP.

Task: write a **read-only inspector** that identifies the real save type and size. The `.sra` is about 4 KB, which is smaller than the usual 32 KB SRAM size, so confirm what it actually is before designing the converter. Then build an importer that copies, never moves. The Project64 1.6 save states will not load in the new app; only the in-game save is carried across. Say so in the UI.

## 9. Google Drive sync

### 9.1 Principles

- **Local first.** Drive is an optional extra. With no sign-in the app works fully, with no pop-ups or repeated reminders.
- Only small files are uploaded: in-game saves, save states, settings and profiles. **Never game files.**
- Uploading is quiet and never slows a game.

### 9.2 What syncs and when

- **Triggers:** when the emulator closes, and every 1 hour while it is running.
- **What:** last 10 in-game saves and last 10 save states per game, plus settings and control profiles.
- Upload only changed files (compare hashes). Wait until the game has finished writing a file before copying.
- **Offline or failure:** save locally, queue, and retry on the next launch or when the connection returns.
- **Conflicts:** keep both versions and ask. Never silently overwrite.

### 9.3 First-time setup (target: about 30 seconds)

1. A card says "Back up your saves to Google Drive" with one **Connect** button.
2. The browser opens Google's sign-in. The user approves; the app is reconnected automatically.
3. The app creates the folder "Memories", holding "N64", in Drive and shows its location.
4. It lists what it found (for example "Harvest Moon 64: 1 save, 10 save states") with an **Upload now** button.

Always visible: a status indicator ("Synced 12 minutes ago") and a **Sync now** button. Settings: auto-sync on or off, what to include, number of versions (default 10).

Other flows: **Restore from Drive** on a new device, plain-language errors with a fix button (offline, signed out, Drive full), and **Disconnect**, which signs out and leaves the files in Drive.

### 9.4 Technical notes

- Use OAuth with the narrowest scope that works. **VERIFY** whether `drive.file` (access only to files the app created) is sufficient; prefer it over wider scopes.
- Store tokens in the operating system's credential store.
- **Token expiry: VERIFY.** For a Google app registered as "external" and left in "testing" status, refresh tokens may expire after about 7 days, which would silently stop syncing. If the owner uses a Google Workspace account, registering the app as an internal one may avoid this. Confirm current rules at setup time.
- **Account separation: recommended.** Because save states contain pieces of game memory, the owner may want gaming files in a separate personal Google account instead of the Workspace account used for the business. In-game saves are tiny and contain no game content, so they are very low risk. Make the account choice clear at connect time. No guarantee about how any provider treats files can be made.
- **Optional encryption** of uploaded files with a key only the user holds (privacy feature). Trade-off: files can't be browsed in Drive and need the app and key to restore.

### 9.5 Settings that sync

Per-game graphics profiles, fill mode, overscan crop, control profiles and general preferences. Small files. Warn that a different screen or GPU may need a small tweak.

## 10. Device-to-device save sharing

Goal: a device can get the latest save from another device it can reach, which may itself have fetched it from Drive.

- **Same home network:** devices discover each other and one offers fresh data to the other.
- **During an online session:** the save can be passed through the netplay connection (section 11).
- **Pairing:** devices pair once with a short code, so only the owner's devices can exchange saves.
- Same rules as everything else: backup first, ask on conflict.
- A device with no connection to anything cannot receive data; it uses its local saves.
- **Cross-device resume uses in-game saves only, never save states.** Continuing on another device (another PC, or a future Android version through Google Drive) picks up from the game's own save. Save states stay with the device that made them, because they only load in the exact same emulator core version.

## 11. Online play (netplay), phase 4

Use existing open-source lockstep netplay approaches. Each player runs the game locally and only controller inputs are exchanged. Rollback is out of scope.

Rules:

1. **All players need the same game file and the same app and core version.** Check automatically before a session and explain mismatches.
2. **Each player keeps their own graphics settings.** Only inputs are shared.
3. **Mixed local and remote players** are supported (for example two on one couch and two remote).
4. **Save states, loading states and rewind are disabled** while connected, because they would desynchronise the session. Show them greyed out with a short explanation.
5. **In-game saves still work.** The **host's save is used** for the session. Take a backup of it before the session starts.
6. When the session ends, the host's save syncs normally. Optionally, guests receive a **copy** of the finished save as a separate version that does not overwrite their own.
7. Pause and reset are host-only or require agreement.
8. States return to normal when the session ends.

Interface and tuning:

- A simple lobby with a short room code, backed by a small relay server so players do not need router settings.
- Show ping and the input delay (in frames) before the match. Let players choose the delay.
- Warn about Wi-Fi and suggest a wired connection.
- Guide to expect (rough, ping to the other player): under about 40 ms very playable; 40 to 80 ms playable for casual games; over about 100 ms hard for fast games such as Smash 64. Mario Party style games tolerate delay well.

## 12. ROM library

- A local folder per device, scanned for N64 files. Identify each by its header and checksum. Cache names and cover art locally.
- Never upload games anywhere. Never download or locate games for the user.
- Games location is a setting behind a storage interface, so a private server or external drive can be added later without a rewrite.
- Moving the collection between devices is a manual copy; the app matches saves to games automatically.
- Do not store or commit game files in the repo.

## 13. Cartridge module (phase 5, optional)

The owner's console may or may not be repairable and wants the option kept open.

- Add as a separate module only after the rest works.
- Decide the route with the owner: repaired console, flash cart, or a separate cartridge reader/writer. **VERIFY** compatibility for each save type (EEPROM vs battery SRAM vs FlashRAM).
- **Always back up the cartridge's current save before any write**, and require explicit confirmation. A failed write could destroy the only genuine save.
- The master copy keeps the raw save form so this stays possible.

## 14. Phases and acceptance criteria

**Phase 0: Spike and decisions.** Compare approaches A, B and C. Pick the core and stack. Write the decision note. Confirm licensing handling. *Done when:* a game boots in a prototype window using the chosen core with correct audio.

**Phase 1: Player.** Core hosted, renderer with presets, fullscreen modes and scaling, basic library and quick menu, input profiles and press-to-join. *Done when:* Harvest Moon 64 and one other game play fully with no visual glitches, at 4x or higher internal resolution, in all fill modes, with no distortion in Fit and Smart zoom.

**Phase 2: Saves.** Save library, read-only inspector, importer for the existing Project64 1.6 save, converters for Project64 (newer) and RetroArch, backups and 10-version history. *Done when:* round-trip tests pass and the owner's existing Harvest Moon 64 save loads in the new app.

**Phase 3: Drive and devices.** Google Drive sync, restore, settings sync, local network pairing. *Done when:* a fresh install on a second device restores saves and settings after sign-in, and offline play queues and later syncs.

**Phase 4: Netplay.** Lobby, relay, lockstep with the rules in section 11. *Done when:* two machines complete a Mario Party session with the host's save and states disabled, and a Smash 64 match is playable at low ping.

**Phase 5: Cartridge (optional).** As in section 13.

## 15. Testing

- Unit tests for converters, with round-trip tests for every format pair.
- Golden-file tests using small test saves (never real owner data, never game files).
- Manual test checklist per phase on the owner's hardware.
- Visual regression capture for each preset and fill mode.
- Simulated failure tests for sync: offline, token expiry, interrupted write, conflicting edits.

## 16. Working rules for the agent

- Ask the owner the **Open questions** below instead of assuming.
- Never modify, move or delete the owner's original saves or game files. Work on copies.
- Do not commit game files, saves, save states or credentials.
- Prefer small, reviewable commits and update this document and `docs/decisions/` when something changes.
- Record every **VERIFY** item as a checked or unchecked task in an issue.

## 17. Open questions for the owner

1. Which "VBA" emulator is the visual reference (VisualBoyAdvance, VBA-M or mGBA), and which part is liked most: the filters, the menu or the fullscreen feel?
2. Screen resolution, refresh rate, operating system and graphics card of the main computer, and of any second device.
3. Which controllers are owned already (gamepads, original N64 controllers with an adapter)?
4. Which games matter most (so far: Harvest Moon 64, Mario Party, Smash Bros)? Which versions and regions?
5. Will two people usually play on one computer, or on separate computers?
6. Which Google account should hold the saves: the main Workspace account, or a separate personal one?
7. For the cartridge option: repaired console, flash cart, or a reader/writer?
8. Roughly how far apart are online players, and is the connection wired?
9. Cover art and game names: bundled, user-supplied, or fetched?
10. ~~App name~~ (answered: the app is **Prometheus 64**; the project is Project Prometheus 64, part of Project Prometheus) and visual identity.
