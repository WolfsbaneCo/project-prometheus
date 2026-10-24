#include "savekit/rom_header.hpp"

#include <cstdio>

namespace savekit {
namespace {

std::uint32_t be32(const Bytes& b, std::size_t at) {
  return (std::uint32_t{b[at]} << 24) | (std::uint32_t{b[at + 1]} << 16) | (std::uint32_t{b[at + 2]} << 8) |
         std::uint32_t{b[at + 3]};
}

}  // namespace

std::string RomHeader::game_id() const {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%08X-%08X-C:%02X", static_cast<unsigned>(crc1), static_cast<unsigned>(crc2),
                static_cast<unsigned>(country));
  return buf;
}

std::optional<RomHeader> parse_rom_header(const Bytes& first_64_bytes) {
  if (first_64_bytes.size() < 0x40) return std::nullopt;
  Bytes h(first_64_bytes.begin(), first_64_bytes.begin() + 0x40);

  RomHeader out{};
  switch (be32(h, 0)) {
    case 0x80371240:
      out.byte_order = RomByteOrder::BigEndian;
      break;
    case 0x37804012:
      out.byte_order = RomByteOrder::ByteSwapped;
      for (std::size_t i = 0; i < h.size(); i += 2) std::swap(h[i], h[i + 1]);
      break;
    case 0x40123780:
      out.byte_order = RomByteOrder::LittleEndian;
      h = swap_words(h);
      break;
    default:
      return std::nullopt;
  }

  out.crc1 = be32(h, 0x10);
  out.crc2 = be32(h, 0x14);
  out.internal_name.assign(reinterpret_cast<const char*>(&h[0x20]), 20);
  while (!out.internal_name.empty() && (out.internal_name.back() == ' ' || out.internal_name.back() == '\0')) {
    out.internal_name.pop_back();
  }
  out.game_code.assign(reinterpret_cast<const char*>(&h[0x3B]), 4);
  out.country = h[0x3E];
  out.version = h[0x3F];
  return out;
}

}  // namespace savekit
