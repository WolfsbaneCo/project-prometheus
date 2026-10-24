# 0002: Licensing of the core and this app

- **Status:** Proposed. Not legal advice; review before any public release.
- **Date:** 2026-10-24
- **Brief sections:** 4.1 (Licensing: VERIFY)

## Facts found in the source (commits listed in 0001)

| Component | Licence as stated in its repository |
|---|---|
| This repository | Apache-2.0 (`LICENSE`) |
| Mupen64Plus-Next (libretro core) | GPL version 2 (`LICENSE`) |
| mupen64plus-core | GPL version 2 (`LICENSES`). Source file headers add "or (at your option) any later version" |
| ParaLLEl-RDP | MIT |
| ParaLLEl-RSP | Multiple licence files (MIT and LGPL present); *VERIFY* which parts apply |
| GLideN64 | GPL (*VERIFY* the version and whether "or later" applies) |
| Qt 6 (proposed) | LGPLv3 for the modules we would use (*VERIFY* per module) |
| SDL3 (proposed) | zlib |

## What that means for us

1. **Before the first build is shared:** the GPL places obligations on distribution, not on development. Building and running the app on our own machines needs nothing extra.
2. **Keep the core separate.** The app loads the core as a separate DLL through the libretro API. We do not compile core code into our binaries, and we do not modify the core. This keeps a clean boundary and makes core updates a file swap.
3. **Once a build is given to anyone else** (including netplay guests), that is distribution:
   - Ship the core's exact source, or a written offer for it, alongside the core DLL.
   - Whether a host that only works with a GPL core counts as a derivative work is disputed. The safe course is to license the host itself under GPL-3.0-or-later before distributing. Apache-2.0 code can be combined into a GPLv3 work. It cannot be combined with GPLv2-only code, so check the "or later" status of every bundled component first (*VERIFY*).
   - Qt under LGPLv3: link dynamically and let users replace the Qt libraries.
4. **Our own save tooling** (`savekit`) contains no code copied from any emulator. The emulators' sources were read only to confirm file layouts, which are facts. The layouts are cited in 0003.
5. Netplay guests receive inputs only, never game files (brief 2 and 11). Game files are never distributed by the app.

## Decision

Load the GPL core as an unmodified separate DLL. The repository stays Apache-2.0 for now. **Open decision for the project owner:** before the first build is distributed, relicense the host as GPL-3.0-or-later (the safe course above), or get legal advice that Apache-2.0 is acceptable. Changing `LICENSE` is deliberately not done here.
