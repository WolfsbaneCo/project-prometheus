# 0011: Game Boy and Game Boy Color games

- **Status:** Accepted (implemented in `apps/prometheus64`; see "Not done").
- **Date:** 2026-11-17
- **Owner's choice:** "the emulator should be able to emulate older Game Boy games too": in the same app and library, linked like the GBA games.

## The core

**Gambatte** (libretro version, GPL-2.0), pinned in `apps/prometheus64/third_party/gambatte/CMakeLists.txt` and built from its sources like gpSP. Chosen over SameBoy and mGBA because it is accurate where commercial games need it, has the Game Boy Color's colours and palettes for original Game Boy games, the MBC3 real-time clock (Pokémon Gold, Silver, Crystal), and a link cable behind two small calls (`gambatte::SerialIO`) that a frontend can carry. SameBoy and TGB Dual link two consoles only inside one emulator; Gambatte's own network link wants a direct connection between the two computers, which home connections rarely allow.

## The link cable over the relay

`third_party/gambatte/net_serial.cpp` replaces Gambatte's direct network link with the libretro netpacket interface, so a Game Boy cable goes wherever a GBA one does: the relay (`link.cpp`, with its resume and goodbye), or a second game on the same computer (`local_link.cpp`).

On a real cable the console that clocks a transfer (the master) receives the other's byte in the same instant. Over a network that byte is a round trip away. Two choices were possible:

1. Stop the emulator inside the transfer until the answer comes (Gambatte's own link does this). The game, its picture and its sound freeze for every byte.
2. **Let the transfer last until the answer comes** (chosen). The master's transfer stays in progress; the game keeps running, waiting for the transfer to end as it does on the console, and it ends when the other side's byte arrives. Nothing freezes; the game simply sees a slow cable.

The other side answers the moment the master's byte arrives, with the byte it has ready if it is waiting for a transfer (its own serial register), and 0xFF if it isn't, as a console that isn't transferring does. Two masters at once each take the other's byte, as with Gambatte's own link.

Three small changes to Gambatte make this work, each applied by the CMake file with a check that stops the build if Gambatte's text has changed:

- `serial_io.h`: the transfer waiting for its answer (`waiting`, `reply`), and the serial registers as the game leaves them (`registers`), so the other side's answer is the byte it has ready.
- `gambatte-memory.cpp`: a transfer still waiting is pushed a little later (0x800 cycles at a time) instead of ending; the incoming byte goes into the register whole when the transfer ends. Gambatte shifted it in bit by bit with the wrong bits whenever the game read the registers mid-transfer (as one waiting for the end does), so the other console's byte arrived garbled: this was a fault in its own network link too.
- `libretro.cpp`: the netpacket interface offered at start-up, and the cable always plugged in (it does nothing until a link starts).

**What it costs:** every byte goes there and back. On one computer a byte takes a frame; over the relay a byte takes a round trip, so far apart (350 ms Portugal to Indonesia) about three bytes a second, and a Pokémon trade, which exchanges a few thousand bytes, takes minutes. That's the Game Boy's cable, not the relay: each byte depends on the one before.

## The rest

| Part | Where |
|---|---|
| Recognising a game | `describe_gb` in `src/library.cpp`: the Nintendo logo at 0x104 and the header checksum at 0x14D. Id `DMG-<title>-<checksums>` (`CGB-` for a Game Boy Color game; the library shows GBC). In archives too |
| Saves | `src/handheld_save.cpp`, shared with the GBA: the cartridge's battery memory (kept as a 128 KB block, `gb_save`) and its clock (`gb_rtc`, 8 bytes), in `Memories\GB`, with history, imports (`.sav`, `.srm`; a VBA-M clock tail is dropped) and Google Drive (`Memories > GB`) |
| Picture | The GBA looks; colour correction and ghosting follow the look (`gambatte_gbc_color_correction`, `gambatte_mix_frames`); 10:9, 144 lines |
| Colours | `gambatte_gb_colorization=auto`: an original Game Boy game gets the palette a Game Boy Color gives it |
| Buttons | The GBA mapping (A, B, Start, Select, D-pad) |

## Checked

With test cartridges made for it in the bench (no game file): 64 bytes exchanged each way between two games on one computer and over a local relay with 350 ms round trips and jitter, every byte right (the first transfer, clocked before the other side was waiting, read 0xFF, as on the console); a battery-backed cartridge's save written, stored in the library and loaded on the next start; the app showing the game at 10:9.

## Not done

- **A commercial game.** None has been played yet; a Pokémon trade over the internet is the thing to try.
- **Super Game Boy borders and the Game Boy Printer.**
- **Trading between a Game Boy and a GBA game:** not possible on the real consoles either.
