# VERIFY checklist

Every **VERIFY** item from the brief, plus the ones the decision notes added. Brief section 16 asks for these to be tracked in an issue: [WolfsbaneCo/project-prometheus-64#1](https://github.com/WolfsbaneCo/project-prometheus-64/issues/1). This file mirrors it so the list is versioned with the code.

## From the brief

- [ ] **Licensing of cores and plugins (4.1).** Partly done in [0002](decisions/0002-licensing.md): licences recorded and handling until the first distribution decided. Still open: "or later" status of GLideN64, which ParaLLEl-RSP licence applies, and Qt module licences.
- [x] **Exact VI options of the chosen renderer (5.1).** ParaLLEl-RDP in Mupen64Plus-Next exposes dither filter, divot filter, gamma dither, VI anti-aliasing, VI bilinear, overscan, native texture LOD, native texrects, super-sampled read-back, deinterlace method, downscaling and synchronous mode. Internal resolution is 1x, 2x, 4x or 8x only. See [0001](decisions/0001-core-and-stack.md).
- [x] **Current save formats of each target (8.3).** Project64, standalone mupen64plus and RetroArch with Mupen64Plus-Next checked against source. See [0003](decisions/0003-save-formats.md).
- [ ] **Is the `drive.file` OAuth scope enough? (9.4)** It must cover "Restore from Drive" on a new install, which needs to find a folder the app created from a different install. Google's scope guide calls `drive.file` non-sensitive and per-file; that files made by one install (or one client of the same Cloud project, such as a later Android one) are visible to another is expected from how Google defines "the app", but not yet checked against real Google. See [0006](decisions/0006-drive-backup.md).
- [x] **Refresh-token expiry for "external" apps in "testing" status (9.4).** Google's own setup docs: refresh tokens expire after 7 days while the consent screen is in Testing, and generally don't once it is In production. The setup guide publishes the app ([`google-drive-setup.md`](google-drive-setup.md)); an internal Workspace app would shut out players outside that Workspace, so the app is External.
- [ ] **Cartridge compatibility per save type (13).** EEPROM, battery SRAM and FlashRAM support, and the dump byte order of the chosen reader, writer or flash cart. Phase 5.

## Added by the decision notes

- [ ] Option B: how RMG and simple64 get the ParaLLEl-RDP frame onto their window, and whether our own shader passes could follow it without changing the plugin.
- [x] Lockstep determinism on the libretro core: synchronous RDP, no host timing leaks. Test in Phase 4 before committing to option A for netplay. Holds with GLideN64 once nothing is copied back from the GPU (`EnableCopyColorToRDRAM` off, depth copy in software): identical console memory between different window sizes and graphics settings, between two computers' copies, and between the Linux and Windows builds. See [0005](decisions/0005-online-play.md).
- [ ] Tauri: whether a web view can be composited cleanly over a Vulkan swapchain on Windows, in case the stack choice is revisited.
- [ ] SDL3 multi-keyboard support on Windows (needed for press-to-join with two keyboards), and mappings for the owner's N64 controller adapter.
- [ ] Project64 1.6's own Controller Pak file name (the importer currently expects `<name>_Cont_N.mpk`).
- [ ] Save layout of the ParaLLEl N64 libretro core, before adding a RetroArch adapter for it.

## Added by the player (Phase 1, see [0004](decisions/0004-player-architecture.md))

Checked so far on Linux with a software GPU (Mesa llvmpipe) and public-domain homebrew test programs, and the Windows build under Wine. Not yet on real hardware or with a commercial game.

- [ ] Sound is clean and in sync in a real game (the audio path is wired and receives the core's samples; never heard).
- [ ] Physical gamepads: press Start to join, the button layout, rumble, and the owner's N64 controller adapter.
- [ ] Remapping a real controller on the Controls screen (the binding logic is unit-tested; capturing a press needs a device).
- [ ] Picture presets and "Auto" resolution on the owner's GPU and screen (open question 2): speed at the screen's own resolution with MSAA 4x and 4xBRZ textures (heaviest on a 4K screen), and the look of Clean, Sharp, Retro and Native.
- [ ] Widescreen fill mode in the owner's games: which ones draw correctly with the 16:9 hack. Harvest Moon 64's intro draws extra scenery correctly; objects popping in at the edges while the camera moves is still to check in normal play.
- [ ] Super resolution on the owner's GPU: frame rate at twice the screen size with 4xBRZ textures.
- [ ] Windows folder and file pickers (O and I on the library screen) and dropping a game file onto the window.
- [ ] Archives from the owner's own collection open (zip, 7z and RAR5 were checked with a real game; RAR4 only with libarchive's own sample file, because current RAR versions can no longer create RAR4 archives).
- [ ] Double-clicking a `.z64` after installing: whether Windows opens Prometheus 64 straight away or asks once (it asks when another program, such as an older emulator, was chosen before).
- [ ] Installer on a clean Windows PC: install, Start menu shortcut, first launch, uninstall leaves `Documents\N64 Saves` alone.
- [ ] Vulkan path for ParaLLEl-RDP: accurate rendering at high resolution, not wired yet.

## Added by online play (Phase 4, see [0005](decisions/0005-online-play.md))

Checked with two copies of the app on one Linux machine through a local copy of the relay, and with a Windows copy (under Wine) joining a Linux host. Not yet over the internet or against a real Cloudflare account.

- [ ] The in-app Cloudflare setup on a real account: the key page opening with both permissions filled in, creating the `workers.dev` address on a brand-new account, installing the relay with its `GROUP_KEY` secret (a `secret_text` binding in the upload), and how long a new address takes to answer. Checked only against a stand-in for Cloudflare's API built from wrangler's own requests.
- [ ] Double-clicking a `.prometheuskey` file on a real Windows PC adds the key (checked by passing the file on the command line, and under Wine).
- [ ] Secure WebSockets (`wss://`) through Windows' own TLS (Schannel) on a real Windows PC. Checked: plain `ws://` under Wine.
- [ ] A Mario Party session and a Smash 64 match between two homes (brief 14, Phase 4): input delay that feels right, how often "Waiting for…" appears on Wi-Fi.
- [ ] Rumble Pak and Controller Pak choice online: each port gets a Rumble Pak when its player's controller can rumble. Games that store data on the Controller Pak would not see it for a rumbling player.
- [ ] Brief 11.6 (optional): give guests a copy of the finished save. Not built; guests' saves are simply left alone.

## Added by Google Drive backup (Phase 3, see [0006](decisions/0006-drive-backup.md))

Checked against a stand-in for Google's sign-in and Drive API (the same requests the app makes): two Linux "computers" and a Windows one under Wine, sharing one Drive.

- [ ] The whole flow against real Google, once the project is registered: the sign-in page, the token exchange (a forum report says Desktop clients need the client secret even with PKCE; the app sends it when the build has one), `about` with only `drive.file`, multipart uploads and the 7-day question after publishing.
- [ ] Windows Credential Manager on a real Windows PC (checked under Wine), and that the browser's return to `127.0.0.1` raises no firewall prompt.
- [ ] Brief 9.5 (settings sync), section 10 (sharing on a home network) and optional encryption: not built yet.
