# Findings: the owner's Project64 1.6 install

The owner uploaded an archive of their Project64 1.6 folder (`Project_64_1.6.rar`). It was extracted to a scratch folder outside the repository and inspected read-only. **No owner files are committed.** This note only describes them.

## Contents

| Item | Found |
|---|---|
| Game files (ROMs) | None in the archive |
| In-game saves | `Save/HARVESTMOON64.sra` (4 KB, 2025-02-04) and `Save/Spring 15/HARVESTMOON64.sra` (4 KB, 2025-01-26). See [0003](../decisions/0003-save-formats.md) |
| Save states | 11 in `Save/` (`.pj.zip`, `.pj0.zip` to `.pj9.zip`) and 10 in `Save/Spring 15/` (`.pj3` is missing there). All Harvest Moon 64, 4 MB console RAM |
| Video plugins | Jabo Direct3D6, Jabo Direct3D8 |
| Input plugins | Jabo DInput, NRage DInput8 v2, Adaptoid v1.0 (a USB adapter for original N64 controllers) |
| Audio plugins | Jabo DSound, Zilmar Audio, No Sound |
| RSP | `RSP.dll` |
| Screenshots | `Harvest Moon 64 (U)`, `Mario Party 3 (U)`, `Super Smash Bros. (U)` |

## What the Direct3D8 error log shows

`Logs/Direct3D8 Error.log` (about 945 KB) is written by the Jabo Direct3D8 plugin. It holds two kinds of errors:

- **11,861 × `SetViewport (scissor) failed 8876086C`.** `8876086C` is `D3DERR_INVALIDCALL`. The plugin asked Direct3D for a clipping rectangle that is partly off-screen or has a negative height (for example `(0,774,1366,-6)`), and Direct3D rejected it. When that happens the plugin drops or mis-clips the draw. This is a likely source of the glitches the owner sees, and it supports the "why not Jabo" decision in the brief.
- **125 × `Failed to create device code = 80004005`** between February and April 2025: the plugin could not start Direct3D 8 at all on the current machine. Direct3D 8 is a 2000-era API that modern Windows only emulates.

The rectangles in the log hint at the displays used over time (to confirm with the owner, open question 2):

| Period | Width × height in the log |
|---|---|
| 2018 | 1366 × 774 (a 1366 × 768 laptop screen) |
| 2022 | 1680 × 1058 (a 1680 × 1050 screen) |
| 2025 | Rectangles up to about 1659 wide, consistent with the 1680 × 1050 screen |

## Implications

- Only the two `.sra` files carry across. The UI must say plainly that the 21 save states cannot be loaded (brief 8.5).
- The `Spring 15` folder is a manual backup. The importer keeps it as a labelled history entry and makes the newer top-level save current.
- The Adaptoid plugin suggests the owner has used original N64 controllers through an adapter at some point (open question 3).
- Mario Party 3 and Super Smash Bros. (both US versions) are the other games played, alongside Harvest Moon 64 (U) (open question 4).
