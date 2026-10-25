# Branding

Project Prometheus has one base mark, the flame. Each app in the project is a variation of it, with its own colour and an acronym.

## Project Prometheus (base mark): `project-prometheus/`

A gold flame on a dark tile.

| File | Use |
|---|---|
| `project-prometheus-icon.svg` | Master icon, 48 px and larger |
| `project-prometheus-icon-small.svg` | 16 to 40 px: the tile fills the square and the flame is larger |
| `project-prometheus.ico` | Windows icon, 16 to 256 px |
| `png/` | 256, 512 and 1024 px exports |

## Prometheus 64: `prometheus64/`

A red flame, the N64 theme colour, with the acronym below it.

| File | Use |
|---|---|
| `prometheus64-icon.svg` | Master icon with **"64"**, 48 px and larger |
| `prometheus64-icon-n64.svg` | Alternative with **"N64"** (see the trademark note below) |
| `prometheus64-icon-small.svg` | 16 to 40 px: flame only, because an acronym cannot be read below about 48 px |
| `prometheus64.ico` / `prometheus64-n64.ico` | Windows icons: the flame only at 16 to 40 px, with the acronym at 48 to 256 px |
| `png/` | 256, 512 and 1024 px exports of both |
| `prometheus64-lockup-on-dark.svg` / `-on-light.svg` | Flame above "PROMETHEUS 64", for splash screens and documents |

**Trademark note:** "N64" and the Nintendo 64 logo are Nintendo trademarks. Emulators usually avoid them (Project64, simple64 and RMG all use plain "64"). The "64" version is the default here; the "N64" version is there if the project owner chooses it.

## Making a new variation

Keep the flame shape, the tile, the corner radius and the layout. Change only:

- the flame gradient and glow colour, and
- the acronym, set in the same lettering as the original logo (cap height 150 units in the 1024 canvas).

## What changed from the original Canva design

- The flame is true vector paths, traced from the bitmap the Canva export embedded. Icons are about 3 to 5 KB instead of 560 KB.
- The app icons drop the white square and the full name, which could not be read below about 64 px. The flame sits on a dark rounded tile with a soft glow, so the icons work on light and dark taskbars.
- The flame fills about 70% of the tile (55% when the acronym sits below it), up from about 40%.
- The "N" did not exist in the original lettering. It is drawn to match, with the same stem width as the original "H". The "6" and "4" are the original shapes.
- `source/prometheus64-original-canva.svg` is the original file, unchanged.

## Colours

| Role | Project Prometheus (gold) | Prometheus 64 (red) |
|---|---|---|
| Flame top | `#FFE08A` | `#FF9F6E` |
| Flame middle | `#F5B642` | `#E8262E` |
| Flame base | `#E8641C` | `#B0121C` |
| Glow (38% opacity) | `#FF8A1F` | `#FF2A2A` |
| Tile, top to bottom | `#2E1F15` to `#110B07` | `#2C1113` to `#0F0607` |
| Acronym and lettering on dark | `#FFE9C2` | `#FFE5E0` |
| Lettering on light | `#2E1F15` | `#2C1113` |
