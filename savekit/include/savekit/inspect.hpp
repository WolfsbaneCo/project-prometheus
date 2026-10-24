#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace savekit {

struct FileReport {
  std::filesystem::path path;
  std::string kind;
  std::uintmax_t size = 0;
  std::string sha256;
  std::vector<std::string> facts;
};

// Describes save files and save states at `path` (a file, or a folder searched recursively).
// Strictly read-only: files are opened for reading and nothing is written, moved or renamed.
std::vector<FileReport> inspect_path(const std::filesystem::path& path);

}  // namespace savekit
