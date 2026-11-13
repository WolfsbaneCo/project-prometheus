// PSP games: recognising a disc image and reading its name and ID, as the PSP does, from the
// PARAM.SFO file every game carries (PSP_GAME/PARAM.SFO on a UMD; first in a PSN game's EBOOT.PBP).
//
// Formats: .iso (a plain UMD image, 2048-byte sectors), .cso (the same, compressed in blocks with
// deflate: the "CISO" format) and .pbp (a PSN or homebrew EBOOT). .chd images are recognised by
// their header only; their name comes from the file name. Only the few kilobytes needed are read,
// never the whole image (they're up to 1.8 GB).
#pragma once

#include <filesystem>
#include <string>

namespace p64::psp {

struct DiscInfo {
  std::string disc_id;  // "ULUS10041": the game's ID, as on the box (without the dash)
  std::string title;    // PARAM.SFO's TITLE, as the PSP's menu shows it
  std::string region;   // from the ID: "USA", "Europe", "Japan", ...
};

// Whether a file is a PSP game image this app plays, by extension.
bool is_psp_extension(const std::filesystem::path& file);
// Reads the game's ID and title. False when the file isn't a PSP game. A .chd is accepted with
// an empty ID (its sectors are compressed in ways this reader doesn't handle).
bool read_disc(const std::filesystem::path& file, DiscInfo& out);
// The region a game ID belongs to (its third letter: ULUS, ULES, ULJM ...).
std::string region_of(const std::string& disc_id);

}  // namespace p64::psp
