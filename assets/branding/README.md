# Prometheus 64 branding

| File | Use |
|---|---|
| `prometheus64-icon.svg` | Master app icon. Use for 48 px and larger: desktop, Start menu, installer, store listings. |
| `prometheus64-icon-small.svg` | Small-size variant: the tile fills the square and the flame is larger, so it stays crisp at 16 to 40 px (taskbar, title bar, file lists). |
| `prometheus64.ico` | Windows icon: 16, 20, 24, 32 and 40 px from the small variant; 48, 64, 96, 128 and 256 px from the master. |
| `png/prometheus64-icon-{256,512,1024}.png` | Raster exports of the master icon, transparent corners. |
| `prometheus64-lockup-on-dark.svg` | Flame above "PROMETHEUS 64", for dark backgrounds (splash screen, the app's own dark interface). |
| `prometheus64-lockup-on-light.svg` | The same lockup with dark lettering, for light backgrounds. |
| `source/prometheus64-original-canva.svg` | The original Canva design the above were made from. Kept unchanged for reference. |

## What changed from the original

- The flame is now true vector paths (traced from the bitmap the Canva export embedded), so it stays sharp at every size and can be edited. The icon files are about 3 KB instead of 560 KB.
- The app icon drops the white square and the "PROMETHEUS 64" text, which was unreadable below about 64 px. It puts the flame on a dark rounded tile with a soft ember glow, so it reads on both light and dark taskbars and matches the app's dark interface.
- The flame fills about 70% of the tile, up from about 40% of the original square. In the small variant it fills about 85%.
- The flat gold is now a gradient from pale gold to ember orange. The original gold `#F5B642` stays as its middle colour and as the lockup's lettering colour.

## Colours

| Role | Value |
|---|---|
| Flame top | `#FFE08A` |
| Flame middle (original gold) | `#F5B642` |
| Flame base | `#E8641C` |
| Glow | `#FF8A1F` at 38% opacity |
| Tile | `#2E1F15` (top) to `#110B07` (bottom) |
| Lettering on light | `#2E1F15` |
