#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "savekit/formats.hpp"
#include "savekit/save_set.hpp"

namespace savekit {

struct ImportOptions {
  // A Project64 folder, its Save folder, or a single save file. Only ever read.
  std::filesystem::path source;
  // The library root (Project Prometheus uses "Memories/N64" and "Memories/GBA"; earlier versions "N64 Saves").
  std::filesystem::path library;
  bool dry_run = false;
  // Make the newest imported save current even when the library already holds a different
  // current save. The existing one is copied into history first.
  bool replace_current = false;
  std::size_t history_limit = 10;
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
};

struct ImportReport {
  std::vector<std::string> lines;
  // Games whose library already had a different current save that was left in place.
  std::vector<std::string> conflicts;
};

// Copies Project64 in-game saves into the library:
//   <library>/<game>/current/raw/...        the newest save exactly as Project64 wrote it
//   <library>/<game>/current/canonical/...  the same save in canonical form
//   <library>/<game>/history/<time>__<id>/  every imported save, never overwritten
//   <library>/<game>/info.json
// Source files are opened read-only and never changed. Save states are reported, not copied.
ImportReport import_project64(const ImportOptions& options);

// The game's current save, if the library has one. `game` is the ROM's internal header name.
std::optional<SaveSet> load_current(const std::filesystem::path& library, const std::string& game);

struct StoreOptions {
  std::filesystem::path library;
  std::string game;  // the ROM's internal header name
  SaveSet save;
  std::string source = "Project Prometheus";
  std::size_t history_limit = 10;
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
};

struct StoreResult {
  bool changed = false;  // false when the save equals the current one
  std::string id;
};

// Makes `save` the game's current save and records it in history. The previous current save is
// already in history (or is copied there first), so nothing is lost.
StoreResult store_current(const StoreOptions& options);

// Games in the library that have a current save, by folder name, sorted.
std::vector<std::string> library_games(const std::filesystem::path& library);

struct ExportOptions {
  std::filesystem::path library;
  std::string game;  // a folder name from library_games()
  Format format = Format::RetroArchMupen64PlusNext;
  std::filesystem::path out_dir;
  // File name without extension. Emulators look saves up by name: RetroArch by the game
  // file's name, Project64 by the ROM's internal name, mupen64plus by its database name.
  std::string base_name;
  // Replace files that already exist. Each one is copied to "<name>.bak-<time>" first.
  bool replace_existing = false;
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
};

struct ExportReport {
  std::vector<std::filesystem::path> written;
  // Files that already exist. When replace_existing is false and this is not empty,
  // nothing was written.
  std::vector<std::filesystem::path> existing;
  std::vector<std::string> lines;
};

// Writes a game's current save in another emulator's format.
ExportReport export_current(const ExportOptions& options);

// Copies `path` to "<path>.bak-<time>" and returns the copy's path.
std::filesystem::path back_up_file(const std::filesystem::path& path, std::chrono::system_clock::time_point now);

}  // namespace savekit
