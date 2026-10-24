# 0001: Emulator core, hosting approach and stack

- **Status:** Proposed. The Phase 0 exit test (a game boots in a prototype window with correct audio) is **not met yet**: it needs the owner's Windows PC, GPU and game files, none of which are available to the agent.
- **Date:** 2026-10-24
- **Brief sections:** 3, 4, 5, 11, 14 (Phase 0)

## How the spike was done

This was a desk spike, not a running prototype. The agent read the current source of the candidate cores and checked what each one actually exposes, rather than relying on memory:

| Source | Commit read |
|---|---|
| `libretro/mupen64plus-libretro-nx` (Mupen64Plus-Next) | `15bada8`, 2026-10-22 |
| `mupen64plus/mupen64plus-core` | `3be6336`, 2026-10-19 |
| `project64/project64` | `24376e8`, 2026-09-24 |

Items marked *verified* below were confirmed in that source. Items marked *VERIFY* still need a check.

## Options against the criteria

| Criterion | A. libretro host (Mupen64Plus-Next) | B. Embed mupen64plus + ParaLLEl plugin | C. Wrap Project64 or RMG |
|---|---|---|---|
| Own scaling and shaders | **Yes.** The ParaLLEl-RDP renderer hands each finished frame to the host as a Vulkan image (`set_image` in `mupen64plus-video-paraLLEl/rdp.cpp`, *verified*). Fill modes, ambient fill, Lanczos downscale, SMAA and sharpening all stay ours. | Partly. The video plugin presents its own frame through the core's video extension API. Putting our own passes after it most likely means changing the plugin (*VERIFY* how RMG does it). | No. Limited to what the wrapped app offers. |
| Accuracy | ParaLLEl-RDP + ParaLLEl-RSP, the same as B. | Same. | Project64: depends on its plugin. RMG: same as B. |
| VI controls (brief 5.1) | Core options, *verified*: `-parallel-rdp-dither-filter`, `-divot-filter`, `-gamma-dither`, `-vi-aa`, `-vi-bilinear`, `-overscan`, `-native-texture-lod`, `-native-tex-rect`, `-super-sampled-read-back`, `-deinterlace-method`, `-downscaling`, `-synchronous`. | Same renderer, so the same controls, set through the plugin's config. | Whatever the UI offers. |
| Texture packs (brief 5.1) | ParaLLEl-RDP has none. The same core also ships **GLideN64**, which has hi-res texture options (`-txHiresEnable` and related, *verified*). A per-game renderer switch gives packs where they exist. | Possible with a second plugin (GLideN64). | Depends. |
| Saves (brief 8) | **Best fit.** The core exposes the whole save memory as one buffer (`RETRO_MEMORY_SAVE_RAM`, *verified*), so our save library is the only thing that reads or writes save files. | The core writes its own files; we would watch and convert them. | Same as B, with less control. |
| Netplay (brief 11) | We implement lockstep in the host: collect every player's input for frame N, then run frame N. It needs deterministic emulation: synchronous RDP and no host timing leaks (*VERIFY* in the prototype). | **Strongest.** mupen64plus-core already has lockstep netplay with a relay server, used by simple64 and RMG. | Whatever the app offers. |
| Input | Host owns it fully (`retro_input_state`, rumble interface). | Host owns it through an input plugin. | Limited. |
| Licensing | See 0002. The core is a separate DLL loaded at runtime. | Same, but more GPL code compiled into our own process. | Fewest obligations. |
| Maintenance | libretro API is stable; the core is actively maintained; we pin a version (netplay needs identical versions anyway). | We track the core's C API and plugin ABI ourselves. | Lowest, but blocked by goals 1 to 3. |

## Decision (proposed)

**A: a native libretro host running Mupen64Plus-Next, with ParaLLEl-RDP (Vulkan) as the default renderer.** B stays the documented fallback for netplay. If lockstep on libretro proves non-deterministic in Phase 4, we move the core host to B behind the same `CoreHost` interface (brief 4.2), and the UI, renderer and save library are unaffected.

C is rejected: it cannot meet goals 1 to 3 (picture, fullscreen and interface).

### Consequences to plan for

1. **Internal resolution is 1x, 2x, 4x or 8x only** with ParaLLEl-RDP (*verified*: those are the only values of `-parallel-rdp-upscaling`). The brief's table (5x, 6x, 9x) is not directly reachable. Plan per display height, NTSC 240 lines:

   | Display | Render | Then |
   |---|---|---|
   | 1080 | 8x (1920 lines) | Lanczos downscale x0.5625. Lighter fallback: 4x (960), then a 1.125 high-quality upscale |
   | 1440 | 8x (1920) | Lanczos downscale x0.75 |
   | 2160 | 8x (1920) | 1.125 high-quality upscale (9x is not available) |

   8x is GPU-heavy. The first-run setup has to measure frame time and step down to 4x on weaker GPUs. GLideN64 allows any factor, at some cost in accuracy.
2. **Two GPU APIs in the host.** ParaLLEl-RDP uses Vulkan; GLideN64 (for texture packs) uses OpenGL through libretro. The renderer has to accept both and feed both into one presentation chain (GL-to-Vulkan interop, or a GL presenter for GLideN64 games). Phase 1 can ship Vulkan only and add GLideN64 when a texture pack is wanted.
3. **The save library owns all save files.** It loads the canonical master copy into `RETRO_MEMORY_SAVE_RAM` at boot and writes it back on change, on exit and on the sync timer. This is why `savekit` exists ahead of the player.

## Stack (proposed)

**C++20, Qt 6 (Qt Quick for the interface), our own Vulkan renderer, SDL3 for controllers and audio, CMake, MSVC on Windows.**

- libretro and Vulkan are C APIs; a C++ host uses them directly with no binding layer.
- Qt Quick draws through Qt's GPU layer (Vulkan, D3D11 or D3D12), has smooth animation and supports keyboard and controller focus navigation. No browser engine is involved, which meets brief 4.3.
- Tauri was considered. Its UI runs in WebView2, an embedded Edge browser engine. The brief allows that, but laying a web view over a Vulkan swapchain for the in-game quick menu is awkward (*VERIFY* before ruling it out completely). It would also add Rust and TypeScript next to the C core API.
- SDL3: controller hotplug, rumble, a large controller mapping database (covers many USB N64 adapters, *VERIFY* for the owner's), and audio.
- Press-to-join with two keyboards needs per-device keyboard input, which on Windows means Raw Input. SDL3 has multi-keyboard support (*VERIFY* on Windows).
- Qt is used under LGPLv3 with dynamic linking; SDL3 is zlib-licensed.

## Next steps to close Phase 0

1. `apps/player-proto`: a Qt window, a libretro loader, Vulkan hardware-render negotiation, SDL3 audio, keyboard input.
2. Run it on the owner's PC with Harvest Moon 64. Done when it boots with correct audio (brief 14).
3. Record the GPU frame time at 4x and 8x on that PC to settle the default resolution.
