// The games the player has added: every N64 game file in the games folder, identified by its
// ROM header (never by file name).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace p64 {

struct Game {
  std::filesystem::path path;
  std::string title;          // from the file name, cleaned up for display
  std::string internal_name;  // from the ROM header; also the save library's folder name
  std::string id;             // CRC1-CRC2-C:country, like N64 ROM databases use
  std::string region;         // "USA", "Europe", "Japan", ...
};

std::vector<Game> scan_games(const std::filesystem::path& folder);
bool is_game_file(const std::filesystem::path& file);
// Reads one game file's header. Returns false when it is not an N64 game.
bool read_game(const std::filesystem::path& file, Game& out);

}  // namespace p64
