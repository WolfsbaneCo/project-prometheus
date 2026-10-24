# 0003: Save formats, the canonical master copy, and the owner's Harvest Moon 64 save

- **Status:** Accepted for the formats below; open items are marked *VERIFY*.
- **Date:** 2026-10-24
- **Brief sections:** 8.3, 8.4, 8.5, 13
- **Code:** `savekit/src/formats.cpp`, tests in `tests/roundtrip_tests.cpp`

## The owner's `.sra` (brief 8.5)

The read-only inspector (`n64save inspect`) was run on a scratch copy of the uploaded Project64 1.6 folder. The owner's files were not modified and are not in this repository.

| File | Size | Finding |
|---|---|---|
| `Save/HARVESTMOON64.sra` | 4096 bytes, modified 2025-02-04 | Real Harvest Moon 64 SRAM. With each 4-byte word reversed, it starts with the text `PACKINSOFT FARM2` (Pack-In-Soft, the developer). Data runs from 0x000 to 0xFFD; all eight 512-byte blocks hold data. |
| `Save/Spring 15/HARVESTMOON64.sra` | 4096 bytes, modified 2025-01-26 | An older copy the owner kept by hand (1059 bytes differ). Same format. |
| `*.pj.zip`, `*.pj0.zip` to `*.pj9.zip` (21 in total, both folders) | about 700 KB each | Project64 save states, each holding about 4.2 MB (4 MB console RAM, no Expansion Pak). Not portable. |

**Why it is 4 KB and not 32 KB:** Harvest Moon 64 is an SRAM (256 Kbit, 32 KB) game. Project64 opens the `.sra` without truncating or pre-sizing it, and writes each byte at `offset ^ 3` (*verified* in current Project64, `N64System/SaveType/Sram.cpp`; the owner's 1.6 file is short and word-swapped in exactly the same way). So the file only grows as far as the game has written, and this game only uses the first 4 KB of the chip.

**Consequence:** the 4 KB file cannot simply be copied into another emulator. mupen64plus reads the `.sra` with a fixed 32 KB read; a short file fails that read *without* triggering its "format fresh SRAM" path, so the remaining 28 KB stays uninitialised memory (*verified*: `open_file_storage` → `read_from_file` returns `file_read_error`, and `format_sram` only runs on `file_open_error`). The converter therefore pads to 32 KB with 0xFF, the value mupen64plus uses for blank SRAM.

## Byte order: what each emulator writes on a Windows PC

The N64 is big-endian. Emulators running on little-endian PCs keep SRAM and FlashRAM as host-order 32-bit words, which reverses every group of four bytes. EEPROM and Controller Pak data pass through byte by byte and are never reversed.

| Format | EEPROM | SRAM | FlashRAM | Controller Pak | Files |
|---|---|---|---|---|---|
| **Canonical (ours)** | as is | N64 order | N64 order | as is | `eeprom.bin`, `sram.bin`, `flashram.bin`, `controller_pak_N.bin`, each full size |
| **Project64** (1.6 to current) | as is, grows as written (max 2 KB) | word-swapped, grows as written | word-swapped, grows as written | as is, 32 KB per port | `<name>.eep`, `.sra`, `.fla`, `<name>_Cont_N.mpk` |
| **mupen64plus** standalone | as is, always 2 KB | word-swapped, 32 KB | word-swapped, 128 KB | as is, 4 × 32 KB in one file | `<name>.eep`, `.sra`, `.fla`, `.mpk` |
| **RetroArch + Mupen64Plus-Next** | as is | word-swapped | word-swapped | as is | one `.srm` of 0x48800 bytes: EEPROM 0x800 at 0, Controller Paks 4 × 0x8000 at 0x800, SRAM 0x8000 at 0x20800, FlashRAM 0x20000 at 0x28800 |

Evidence (*verified* at the commits in 0001):

- Project64: `Sram.cpp` writes at `(offset + i) ^ 3`; `FlashRam.cpp` copies host-order 32-bit words; `Eeprom.cpp` reads and writes `m_EEPROM[0x800]` byte by byte; `Mempak.cpp` names files `"%s_Cont_%d.mpk"` and uses 0x8000 bytes.
- mupen64plus-core: `sram.c` and `flashram.c` store bytes at `addr ^ S8`, where `S8` is 3 on little-endian hosts; `main.c` opens `.eep` at `EEPROM_MAX_SIZE` 0x800 ("EEP files are all EEPROM_MAX_SIZE bytes long") and `.mpk` at 4 × `MEMPAK_SIZE`; `format_eeprom`, `format_sram` and `format_flashram` fill with 0xFF.
- Mupen64Plus-Next: `libretro/libretro_memory.h` defines `save_memory_data { eeprom[0x800]; mempack[4 × 0x8000]; sram[0x8000]; flashram[0x20000]; }`, and `RETRO_MEMORY_SAVE_RAM` returns that struct, which RetroArch writes as the `.srm`.

**Result:** Project64, mupen64plus and RetroArch (Mupen64Plus-Next) already agree on byte order for every memory type. They differ in file sizes, padding and packaging. The often-repeated advice to "byte-swap Project64 saves for RetroArch" does not apply to these current versions. It may apply to other cores or to old versions (*VERIFY* before adding a ParaLLEl N64 adapter).

## The canonical master copy

- One file per memory, in N64 (big-endian) byte order, at full chip size. This is also the order a cartridge reader or writer dumps SRAM and FlashRAM in (*VERIFY* per device in Phase 5), so the cartridge route stays open (brief 13).
- EEPROM is held at 2 KB; 4 Kbit games use only the first 512 bytes.
- Memories the game never wrote (entirely 0xFF, or entirely 0x00 for a Controller Pak) are left out, so a Harvest Moon save produces only `sram.bin`.
- The original raw files are kept next to the canonical copy (`current/raw/`), together with their exact lengths (the "shape" in `info.json`). Converting back to Project64 with that shape gives a byte-identical file, including the short 4 KB length.

## Safety rules as implemented

- **Round-trip tests** cover every ordered pair of the four formats on seven synthetic saves, plus short Project64 files (4096, 4093, 1 and 32765 bytes). Each test runs A → B → A and requires byte-identical output. Golden vectors pin the exact byte order and offsets. CI runs them on Windows and Linux (`.github/workflows/ci.yml`).
- **The importer only reads its source**, refuses a library folder anywhere inside the emulator folder it was pointed at (or the reverse), and writes every new entry to a `.partial` folder that is renamed into place only once complete. Every written file is read back and compared.
- **The history is never overwritten.** Each import is stored as `history/<UTC time>__<content id>/`. Re-importing the same save does nothing. Only the oldest entries beyond the limit (default 10) are removed.
- **No silent overwrite of `current/`.** If `current/` is an earlier untouched import, a newer Project64 save replaces it; the earlier one is already in history. `current/` never moves back to an older save, for example when the newer file has disappeared from the Project64 folder. If `current/` came from anywhere else, the import stops with a conflict. With `--replace-current`, the existing copy is backed up into history before it is replaced.
- **Converting never overwrites** an existing file unless `--overwrite` is given, and even then the old file is backed up first.

## Known gaps

- A RetroArch `.srm` made from a save without Controller Pak data holds unformatted (all-zero) paks. Games that use the pak will offer to format it. Mupen64Plus-Next itself would have created formatted paks. Generating a formatted blank pak is a later improvement.
- The 96 KB SRAM of Dezaemon 3D is not supported.
- Project64 1.6's own Controller Pak file name is not confirmed. The importer recognises `<name>_Cont_N.mpk` and reports any other `.mpk` without importing it (*VERIFY* with a 1.6 install that has a pak file).
- Game identity: Project64 names saves after the ROM's internal header name (`HARVESTMOON64`). `info.json` records that name now, and the ROM ID (`CRC1-CRC2-C:country`, for example `98DF9DFC-6606C189-C:45` for Harvest Moon 64 (U) in Project64's database) once the ROM is scanned (`n64save rom-id`).
