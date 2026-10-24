#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "savekit/bytes.hpp"

namespace savekit {

// How a ROM dump's bytes are ordered on disk.
enum class RomByteOrder {
  BigEndian,     // .z64, the N64's own order
  ByteSwapped,   // .v64, every 16-bit pair swapped
  LittleEndian,  // .n64, every 32-bit word reversed
};

// The identifying parts of an N64 ROM header. Games are identified by these, never by file name.
struct RomHeader {
  RomByteOrder byte_order;
  std::uint32_t crc1;
  std::uint32_t crc2;
  std::string internal_name;  // 20 bytes at 0x20, trailing spaces trimmed
  std::string game_code;      // 4 bytes at 0x3B, e.g. "NYWE"
  std::uint8_t country;       // 0x3E
  std::uint8_t version;       // 0x3F

  // "98DF9DFC-6606C189-C:45": the same key Project64's ROM database (Project64.rdb) uses.
  std::string game_id() const;
};

// Parses the first 64 bytes of a ROM. Returns nullopt when the data is not an N64 ROM header.
// Reads only the header; the caller should pass only the first 64 bytes of the file.
std::optional<RomHeader> parse_rom_header(const Bytes& first_64_bytes);

}  // namespace savekit
