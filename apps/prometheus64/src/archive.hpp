// Reads files packed in .zip, .7z and .rar archives (through libarchive). Nothing is ever written:
// a game is unpacked into memory when it starts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace p64::archive {

bool is_archive(const std::filesystem::path& file);

struct Entry {
  std::string name;  // path inside the archive, UTF-8
  int64_t size = 0;  // -1 when the archive does not say
};

// Calls `visit` with each regular file and its first `head` bytes (fewer for shorter files).
// `visit` returns false to stop early. Returns an error message, or empty on success.
std::string scan(const std::filesystem::path& file, std::size_t head,
                 const std::function<bool(const Entry&, const std::vector<uint8_t>&)>& visit);

// Unpacks one file from the archive. Refuses files over `max_size` bytes.
std::string extract(const std::filesystem::path& file, const std::string& name, std::size_t max_size,
                    std::vector<uint8_t>& out);

}  // namespace p64::archive
