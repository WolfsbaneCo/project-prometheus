# 0002: Licensing of the core and this app

- **Status:** Proposed. Not legal advice; review before any public release.
- **Date:** 2026-10-24
- **Brief sections:** 4.1 (Licensing: VERIFY)

## Facts found in the source (commits listed in 0001)

| Component | Licence as stated in its repository |
|---|---|
| This repository | GPL-3.0-or-later (`LICENSE`), Apache-2.0 until 2026-10-30 |
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

## Owner's choice: free only, name and logo reserved (2026-10-30)

Before the repository can go public, the owner asked to stop people charging for the app. No open source licence can forbid selling, including Apache-2.0 and the cores' GPL, and README wording alone can't take that right away. The owner chose to keep an open licence for the code and reserve the name and logo instead:

- [`TRADEMARKS.md`](../../TRADEMARKS.md): "Project Prometheus", "Prometheus 64" and the flame logo are Wolfsbane Co.'s trademarks. The licence grants no rights to them (Apache-2.0 section 6 at first; now an additional term under GPL-3 section 7(e), in `NOTICE`). The files in `assets/branding/` are all rights reserved. The unchanged official installer may be shared free of charge. Selling under the name or logo, and modified versions under them, are not allowed.
- [`NOTICE`](../../NOTICE): redistributors must keep it (Apache-2.0 section 4(d) at first; now an additional term under GPL-3 section 7(b)). It states the official source, that the app is free, that it contains no games or BIOS, and that it isn't affiliated with any console maker.
- `installer/TERMS.txt`: terms of use the installer asks the player to accept (free only, no games included, not affiliated, no warranty, the player's own accounts).
- The README's **Legal** section says the same for visitors.

**Limit:** someone can still sell a renamed build with other icons, as every open source licence allows. Stopping that would take a non-commercial licence for the app's own code, which the owner declined.

## Owner's choice: GPL-3.0-or-later (2026-10-30)

The owner wanted the GBA Smooth look to match VBA-M's, which is xBRZ (Zenju), licensed under the GNU GPL version 3. Building it into the app makes the app a GPL-3 work, so the owner chose to relicense the app's own code from Apache-2.0 to **GPL-3.0-or-later**, which is also the safe course this note recommended for a host that works with GPL cores.

- `LICENSE` is the GPL version 3 text. `NOTICE` carries two additional terms allowed by its section 7: keep the NOTICE (7(b)), and no rights to the name and logo (7(e)). `TRADEMARKS.md`, `installer/TERMS.txt` and the README's Legal section say the same.
- **Compatibility:** gpSP's files are "GPL version 2 or any later version", as are mupen64plus-core's (above); both are loaded as separate libraries anyway. MIT, BSD, zlib and OFL components (SDL, libarchive, stb, fonts, MMPX) can be combined into a GPL-3 work.
- **What changes for others:** anyone may still share, change and sell copies, as with Apache-2.0, but copies and changed versions must be GPL-3 too, with their source. The name and logo stay reserved.
- xBRZ is kept as published in `apps/prometheus64/third_party/xbrz` (commit and source in its README and in the third-party notices).

