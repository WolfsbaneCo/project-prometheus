#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace savekit {

struct ImportOptions {
  // A Project64 folder, its Save folder, or a single save file. Only ever read.
  std::filesystem::path source;
  // The library root ("N64 Saves").
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

}  // namespace savekit
