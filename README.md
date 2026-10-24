# evercart-64

A clean, modern desktop N64 player for a personal game collection (working title). It hosts a proven emulator core and adds its own picture pipeline, interface, controls, portable save library and optional backups.

- **What to build:** [`docs/BRIEF.md`](docs/BRIEF.md), the single source of truth
- **Decisions:** [`docs/decisions/`](docs/decisions/)
  - [0001 Core, hosting approach and stack](docs/decisions/0001-core-and-stack.md) (proposed)
  - [0002 Licensing](docs/decisions/0002-licensing.md) (proposed)
  - [0003 Save formats and the canonical master copy](docs/decisions/0003-save-formats.md)
- **Waiting on the owner:** [`docs/open-questions.md`](docs/open-questions.md)
- **Unverified assumptions:** [`docs/verify.md`](docs/verify.md)

## Status

| Phase | State |
|---|---|
| 0: Spike and decisions | Decision notes written. Prototype boot on the owner's PC still to do |
| 1: Player | Not started |
| 2: Saves | Started: `savekit` library, read-only inspector, Project64 importer, converters, round-trip tests |
| 3 to 5 | Not started |

## Save tools (`n64save`)

Build (CMake 3.20+, a C++20 compiler; no other dependencies):

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Use:

```sh
# Describe saves and save states; strictly read-only
n64save inspect "C:/Games/Project 64 1.6/Save"

# Copy Project64 saves into the library (never moves or edits the originals)
n64save import-pj64 "C:/Games/Project 64 1.6" --library "%USERPROFILE%/Documents/N64 Saves" --dry-run
n64save import-pj64 "C:/Games/Project 64 1.6" --library "%USERPROFILE%/Documents/N64 Saves"

# Convert between formats (refuses to overwrite unless --overwrite, which backs up first)
n64save formats
n64save convert --from canonical --to retroarch-mupen64plus-next --name "Harvest Moon 64 (USA)" \
  --out ./out "N64 Saves/HARVESTMOON64/current/canonical/sram.bin"

# Identify a ROM by header and checksum (reads only the first 64 bytes)
n64save rom-id "Harvest Moon 64 (USA).z64"
```

## Ground rules

Game files, saves, save states and credentials never go into this repository (see `.gitignore`). Tests use synthetic data only.
