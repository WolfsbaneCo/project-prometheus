# VERIFY checklist

Every **VERIFY** item from the brief, plus the ones the decision notes added. Brief section 16 asks for these to be tracked in an issue: [WolfsbaneCo/project-prometheus-64#1](https://github.com/WolfsbaneCo/project-prometheus-64/issues/1). This file mirrors it so the list is versioned with the code.

## From the brief

- [ ] **Licensing of cores and plugins (4.1).** Partly done in [0002](decisions/0002-licensing.md): licences recorded and handling until the first distribution decided. Still open: "or later" status of GLideN64, which ParaLLEl-RSP licence applies, and Qt module licences.
- [x] **Exact VI options of the chosen renderer (5.1).** ParaLLEl-RDP in Mupen64Plus-Next exposes dither filter, divot filter, gamma dither, VI anti-aliasing, VI bilinear, overscan, native texture LOD, native texrects, super-sampled read-back, deinterlace method, downscaling and synchronous mode. Internal resolution is 1x, 2x, 4x or 8x only. See [0001](decisions/0001-core-and-stack.md).
- [x] **Current save formats of each target (8.3).** Project64, standalone mupen64plus and RetroArch with Mupen64Plus-Next checked against source. See [0003](decisions/0003-save-formats.md).
- [ ] **Is the `drive.file` OAuth scope enough? (9.4)** Check before Phase 3. It must cover "Restore from Drive" on a new install, which needs to find a folder the app created from a different install.
- [ ] **Refresh-token expiry for "external" apps in "testing" status (9.4).** Check the current Google rules, and whether an internal Workspace app avoids the expiry. Depends on open question 6.
- [ ] **Cartridge compatibility per save type (13).** EEPROM, battery SRAM and FlashRAM support, and the dump byte order of the chosen reader, writer or flash cart. Phase 5.

## Added by the decision notes

- [ ] Option B: how RMG and simple64 get the ParaLLEl-RDP frame onto their window, and whether our own shader passes could follow it without changing the plugin.
- [ ] Lockstep determinism on the libretro core: synchronous RDP, no host timing leaks. Test in Phase 4 before committing to option A for netplay.
- [ ] Tauri: whether a web view can be composited cleanly over a Vulkan swapchain on Windows, in case the stack choice is revisited.
- [ ] SDL3 multi-keyboard support on Windows (needed for press-to-join with two keyboards), and mappings for the owner's N64 controller adapter.
- [ ] Project64 1.6's own Controller Pak file name (the importer currently expects `<name>_Cont_N.mpk`).
- [ ] Save layout of the ParaLLEl N64 libretro core, before adding a RetroArch adapter for it.

## Added by the player (Phase 1, see [0004](decisions/0004-player-architecture.md))

Checked so far on Linux with a software GPU (Mesa llvmpipe) and public-domain homebrew test programs, and the Windows build under Wine. Not yet on real hardware or with a commercial game.

- [ ] Sound is clean and in sync in a real game (the audio path is wired and receives the core's samples; never heard).
- [ ] Physical gamepads: press Start to join, the button layout, rumble, and the owner's N64 controller adapter.
- [ ] Picture presets and "Auto" internal resolution on the owner's GPU and screen (open question 2): speed at 5x and the look of Clean, Sharp, Retro and Native.
- [ ] Widescreen fill mode in the owner's games: which ones draw correctly with the 16:9 hack.
- [ ] Windows folder and file pickers (O and I on the library screen) and dropping a game file onto the window.
- [ ] Installer on a clean Windows PC: install, Start menu shortcut, first launch, uninstall leaves `Documents\N64 Saves` alone.
- [ ] Vulkan path for ParaLLEl-RDP: accurate rendering at high resolution, not wired yet.
