# 0004: How the player is built

- **Status:** Accepted for Phase 1 (implemented in `apps/prometheus64`).
- **Date:** 2026-10-25
- **Brief sections:** 3, 5, 6, 7, 8
- **Builds on:** [0001](0001-core-and-stack.md) option A (host the Mupen64Plus-Next libretro core)

## What it is

Prometheus 64 is its own program: a C++20 app on SDL3 (window, audio, gamepads, dialogs) and OpenGL 3.3, with its own interface drawn on the GPU. The emulation itself is the Mupen64Plus-Next core, loaded at run time from `cores\mupen64plus_next_libretro.dll` next to the program. The app talks to the core only through the libretro API (`apps/prometheus64/src/core.cpp`). The rest of the app never sees that API.

| Part | Where | What it does |
|---|---|---|
| Core host | `core.cpp` | Loads the DLL, answers its requests (options, folders, OpenGL context, rumble), runs frames |
| Picture | `video.cpp` | The core draws into our framebuffer at the internal resolution; we compose it onto the screen with the fill mode, sharpening and scanlines |
| Sound | `audio.cpp` | One SDL audio stream; drops a batch instead of drifting behind when the screen runs faster than 59.94 Hz |
| Players | `input.cpp` | Four seats, press Start to join, split keyboard, rumble |
| Saves | `app.cpp` + `savekit` | Reads and writes the core's save memory; the save library is the only place saves live |
| Interface | `ui.cpp`, `app.cpp` | Library, quick menu, "Who's playing", help, notices |

## Choices and why

- **Renderer per preset.** Clean, Sharp and Retro use **GLideN64** (high internal resolution, MSAA, texture enhancement, texture packs). Native uses **Angrylion**, a pixel-exact software renderer, at 1x with the console's dithering. **ParaLLEl-RDP** (accurate *and* upscaled) needs a Vulkan host. It is compiled into the shipped core, but the app does not use it yet. Adding a Vulkan path is a later step and does not change the rest of the app.
- **Internal resolution "Auto"** renders at exactly the screen's height and the picture's shape (GLideN64's screen-size mode, `EnableNativeResFactor` 0): 1440x1080 on a 1080p screen, 1920x1440 at 1440p. In fullscreen every game pixel lands on one screen pixel. The earlier rule (the next multiple of 240 lines, then shrink with mipmaps) left the whole picture slightly soft. A fixed 1x to 8x can still be chosen per game.
- **Super resolution** (per game) renders at twice the screen's height and shrinks by exactly half through the first mipmap level, a 2x2 average per pixel. It smooths the cut-out edges (trees, sprites) that MSAA cannot reach, so MSAA is off there. It costs four times the pixels, so it is a choice, not the default.
- **Presenting the frame** uses a Catmull-Rom (bicubic) filter, exact at 1:1 and sharper than bilinear when scaled. Mipmaps are used only for frames about twice the size they are shown at or more.
- **Texture filtering** stays N64-accurate (3-point). Standard bilinear was compared on Harvest Moon 64 and looked the same; the thin dark outline around sprites is drawn into the game's own textures.
- **Textures.** Clean and Sharp redraw the game's textures with 4xBRZ (clean outlines, crisp characters), chosen by the owner after comparing 4xBRZ, HQ4X and smoothed variants on Harvest Moon 64. Per game: HD (crisp, 4xBRZ), HD (soft, HQ4X) or Original. MSAA 4x replaces FXAA, which blurred textures. "Continuous texrect coords" is on (Auto) against seams in 2D images.
- **Fill modes are ours, not the core's.** Fit, Smart zoom (crops up to 12 % of the top and bottom to fill more of a wide screen), Widescreen (the core's 16:9 hack, per game), Ambient (a blurred copy of the picture fills the bars) and Stretch.
- **Independent C buttons.** The core's `alt-map` option, so the C buttons work on the right stick and on the keyboard together with the face buttons.
- **Saves.** The core's save memory (`RETRO_MEMORY_SAVE_RAM`) is filled from the library when a game starts. While playing, each change is written back once it has been stable for one second, with the last 10 versions kept (see [0003](0003-save-formats.md)). A Controller Pak that holds no notes is not written. The core's own save folder is scratch space.
- **Archives.** Games in `.zip`, `.7z` and `.rar` (RAR4 and RAR5) archives are read with libarchive, built in with zlib and liblzma. Every file in an archive is checked by its ROM header, whatever it is called. A game is unpacked into memory when it starts: nothing is written next to the player's files, and the core only ever receives the game's bytes.
- **Double-click.** The installer can make Prometheus 64 the program for `.z64`, `.n64` and `.v64` files (a task the player can untick). It only adds itself to "Open with" for `.zip`, `.7z` and `.rar`, so those keep their own program.
- **One core load per game.** The core cannot load a second game in the same session, so the app reloads the DLL between games and when a renderer change needs a restart. It keeps the game's progress through a save state.
- **Save states** go in `<library>\<game>\states\slotN.state`, with a note of the core version that made them. A state from a different core version may not load. In-game saves are the durable copy.

## Folders

| What | Where on Windows |
|---|---|
| Program, core and fonts | `%LOCALAPPDATA%\Programs\Prometheus 64` (per-user install) |
| Save library | `Documents\N64 Saves` (the same library as the `n64save` tool) |
| Settings | `%APPDATA%\Project Prometheus\Prometheus 64\settings.txt` |
| Texture packs | `%APPDATA%\Project Prometheus\Prometheus 64\system\Mupen64plus\hires_texture\<game's internal name>` |

## Licensing in practice

The installer ships the core DLL unmodified, built from the pinned commit in `tools/build-core-windows.sh`, with `cores\CORE-SOURCE.txt`, the GPLv2 text and a written source offer in `licenses\`. The app itself is a separate program that loads the DLL at run time, and the repository stays Apache-2.0 for now. [0002](0002-licensing.md) still lists the questions the owner decides.
