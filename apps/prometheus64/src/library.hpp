// The games the player has added: every N64 game file in the games folder, plain or packed in a
// .zip, .7z or .rar archive, identified by its ROM header (never by file name).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace p64 {

struct Game {
  std::filesystem::path path;
  std::string entry;          // the game's file inside the archive at `path`; empty for a plain file
  std::string title;          // from the file name, cleaned up for display
  std::string internal_name;  // from the ROM header; also the save library's folder name
  std::string id;             // CRC1-CRC2-C:country, like N64 ROM databases use
  std::string region;         // "USA", "Europe", "Japan", ...
};

std::vector<Game> scan_games(const std::filesystem::path& folder);
// A game file or an archive that may hold games, by extension.
bool is_game_file(const std::filesystem::path& file);
// The games in one file: at most one for a game file, one per game inside an archive.
// `error` says why an archive could not be read (empty when it was read).
std::vector<Game> read_games(const std::filesystem::path& file, std::string* error = nullptr);
// The first game in a file. Returns false when it holds no N64 game.
bool read_game(const std::filesystem::path& file, Game& out);
// The whole game, unpacked when it is in an archive. Returns an error message, or empty on success.
std::string load_game_data(const Game& game, std::vector<uint8_t>& data);

}  // namespace p64
