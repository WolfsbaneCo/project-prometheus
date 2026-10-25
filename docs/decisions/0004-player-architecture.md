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

- **Renderer per preset.** Clean, Sharp and Retro use **GLideN64** (high internal resolution, FXAA, texture packs). Native uses **Angrylion**, a pixel-exact software renderer, at 1x with the console's dithering. **ParaLLEl-RDP** (accurate *and* upscaled) needs a Vulkan host. It is compiled into the shipped core, but the app does not use it yet. Adding a Vulkan path is a later step and does not change the rest of the app.
- **Internal resolution "Auto"** picks the smallest multiple of 240 lines that covers the screen: 5x at 1080p, 6x at 1440p. Each preset can be overridden per game.
- **Fill modes are ours, not the core's.** Fit, Smart zoom (crops up to 12 % of the top and bottom to fill more of a wide screen), Widescreen (the core's 16:9 hack, per game), Ambient (a blurred copy of the picture fills the bars) and Stretch.
- **Independent C buttons.** The core's `alt-map` option, so the C buttons work on the right stick and on the keyboard together with the face buttons.
- **Saves.** The core's save memory (`RETRO_MEMORY_SAVE_RAM`) is filled from the library when a game starts. While playing, each change is written back once it has been stable for one second, with the last 10 versions kept (see [0003](0003-save-formats.md)). A Controller Pak that holds no notes is not written. The core's own save folder is scratch space.
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
