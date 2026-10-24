#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace savekit {

using Bytes = std::vector<std::uint8_t>;

// Reverses the byte order of every 32-bit word. The length must be a multiple of 4.
Bytes swap_words(const Bytes& data);

// True when every byte equals `value`. An empty buffer counts as uniform.
bool is_uniform(const Bytes& data, std::uint8_t value);

// Reads a whole file. Opens it read-only; throws std::runtime_error on failure.
Bytes read_file(const std::filesystem::path& path);

// Writes a whole file and reads it back to confirm the bytes landed.
// Refuses to replace an existing file; callers back up and remove first.
void write_new_file(const std::filesystem::path& path, const Bytes& data);

std::string to_hex(const std::uint8_t* data, std::size_t size);

}  // namespace savekit
