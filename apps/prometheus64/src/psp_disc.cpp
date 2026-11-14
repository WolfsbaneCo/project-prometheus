#include "psp_disc.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <vector>

namespace fs = std::filesystem;

namespace p64::psp {
namespace {

using Bytes = std::vector<uint8_t>;
constexpr uint32_t kSector = 2048;

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint64_t u64(const uint8_t* p) { return u32(p) | static_cast<uint64_t>(u32(p + 4)) << 32; }
uint32_t be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return static_cast<uint64_t>(be32(p)) << 32 | be32(p + 4); }

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

bool read_at(std::ifstream& in, uint64_t at, Bytes& out, std::size_t n) {
  out.assign(n, 0);
  in.clear();
  in.seekg(static_cast<std::streamoff>(at));
  in.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(n));
  return static_cast<std::size_t>(in.gcount()) == n;
}

// Sectors of a disc image, plain (.iso) or compressed (.cso).
using SectorReader = std::function<bool(uint32_t sector, Bytes& out)>;

SectorReader iso_reader(std::ifstream& in) {
  return [&in](uint32_t sector, Bytes& out) { return read_at(in, static_cast<uint64_t>(sector) * kSector, out, kSector); };
}

// CISO: "CISO", header size, total bytes, block size, version, index shift; then one 32-bit
// index entry per block (and one past the last): the block's offset >> shift, top bit set when
// the block is stored uncompressed. Compressed blocks are raw deflate.
bool cso_reader(std::ifstream& in, SectorReader& reader) {
  Bytes head;
  if (!read_at(in, 0, head, 24) || std::memcmp(head.data(), "CISO", 4) != 0) return false;
  const uint64_t total = u64(&head[8]);
  const uint32_t block = u32(&head[16]);
  const int shift = head[21];
  if (block != kSector || total == 0 || shift > 16) return false;
  reader = [&in, total, shift](uint32_t sector, Bytes& out) {
    if (static_cast<uint64_t>(sector) * kSector >= total) return false;
    Bytes idx;
    if (!read_at(in, 24 + static_cast<uint64_t>(sector) * 4, idx, 8)) return false;
    const uint32_t a = u32(&idx[0]), b = u32(&idx[4]);
    const bool plain = a & 0x80000000u;
    const uint64_t from = static_cast<uint64_t>(a & 0x7FFFFFFFu) << shift;
    const uint64_t to = static_cast<uint64_t>(b & 0x7FFFFFFFu) << shift;
    if (to <= from || to - from > 4 * kSector) return false;
    Bytes raw;
    if (!read_at(in, from, raw, static_cast<std::size_t>(to - from))) return false;
    if (plain) {
      raw.resize(kSector);
      out = std::move(raw);
      return true;
    }
    out.assign(kSector, 0);
    z_stream z{};
    if (inflateInit2(&z, -15) != Z_OK) return false;
    z.next_in = raw.data();
    z.avail_in = static_cast<uInt>(raw.size());
    z.next_out = out.data();
    z.avail_out = kSector;
    const int r = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    return r == Z_STREAM_END || (r == Z_OK && z.avail_out == 0) || (r == Z_BUF_ERROR && z.avail_out == 0);
  };
  return true;
}

// A CHD made from a DVD-type disc (what a UMD becomes with chdman createdvd), told by its metadata:
// version 5, the metadata list from the offset at 48, each entry a 4-letter tag, a flag byte, a
// 24-bit length and the offset of the next (big-endian). CD images (PlayStation, Saturn, Mega CD:
// "CHT2", "CHTR", "CHCD") and GD-ROMs (Dreamcast: "CHGD", "CHGT") have other tags and are left out.
// (A PS2 DVD carries the same tag as a UMD; telling those apart would mean decompressing it.)
bool chd_is_dvd(std::ifstream& in) {
  Bytes head;
  if (!read_at(in, 0, head, 64) || be32(&head[12]) != 5) return false;
  uint64_t at = be64(&head[48]);
  for (int i = 0; i < 16 && at != 0; ++i) {
    Bytes entry;
    if (!read_at(in, at, entry, 16)) return false;
    if (std::memcmp(entry.data(), "DVD ", 4) == 0) return true;
    at = be64(&entry[8]);
  }
  return false;
}

// A file in the image's ISO 9660 file system, by path ("PSP_GAME/PARAM.SFO").
bool read_iso_file(const SectorReader& sectors, const std::string& path, Bytes& out) {
  Bytes pvd;
  if (!sectors(16, pvd) || pvd[0] != 1 || std::memcmp(&pvd[1], "CD001", 5) != 0) return false;
  uint32_t extent = u32(&pvd[156 + 2]), size = u32(&pvd[156 + 10]);
  std::size_t at = 0;
  while (at <= path.size()) {
    const std::size_t slash = path.find('/', at);
    const std::string name = path.substr(at, slash == std::string::npos ? std::string::npos : slash - at);
    const bool last = slash == std::string::npos;
    bool found = false;
    for (uint32_t s = 0; s < (size + kSector - 1) / kSector && s < 64 && !found; ++s) {
      Bytes dir;
      if (!sectors(extent + s, dir)) return false;
      for (std::size_t p = 0; p + 33 < kSector;) {
        const uint8_t len = dir[p];
        if (len == 0) break;
        const uint8_t name_len = dir[p + 32];
        std::string entry(reinterpret_cast<const char*>(&dir[p + 33]), std::min<std::size_t>(name_len, kSector - p - 33));
        if (const auto semi = entry.find(';'); semi != std::string::npos) entry.resize(semi);
        if (lower(entry) == lower(name)) {
          extent = u32(&dir[p + 2]);
          size = u32(&dir[p + 10]);
          found = true;
          break;
        }
        p += len;
      }
    }
    if (!found) return false;
    if (last) {
      if (size > 64 * 1024) return false;
      out.clear();
      for (uint32_t s = 0; s * kSector < size; ++s) {
        Bytes sec;
        if (!sectors(extent + s, sec)) return false;
        out.insert(out.end(), sec.begin(), sec.end());
      }
      out.resize(size);
      return true;
    }
    at = slash + 1;
  }
  return false;
}

// PARAM.SFO: "\0PSF", version, key table offset, data table offset, entry count; each entry: key
// offset (16), format (16), length, max length, data offset.
void read_sfo(const Bytes& sfo, DiscInfo& out) {
  if (sfo.size() < 20 || std::memcmp(sfo.data(), "\0PSF", 4) != 0) return;
  const uint32_t keys = u32(&sfo[8]), data = u32(&sfo[12]), count = u32(&sfo[16]);
  for (uint32_t i = 0; i < count && 20 + i * 16 + 16 <= sfo.size(); ++i) {
    const uint8_t* e = &sfo[20 + i * 16];
    const std::size_t key_at = keys + u16(e), value_at = data + u32(e + 12), len = u32(e + 4);
    if (key_at >= sfo.size() || value_at + len > sfo.size()) continue;
    const std::string key(reinterpret_cast<const char*>(&sfo[key_at]), strnlen(reinterpret_cast<const char*>(&sfo[key_at]), sfo.size() - key_at));
    std::string value(reinterpret_cast<const char*>(&sfo[value_at]), len);
    if (const auto nul = value.find('\0'); nul != std::string::npos) value.resize(nul);
    if (key == "TITLE") out.title = value;
    if (key == "DISC_ID") out.disc_id = value;
  }
}

}  // namespace

bool is_psp_extension(const fs::path& file) {
  const std::string ext = lower(file.extension().string());
  return ext == ".iso" || ext == ".cso" || ext == ".pbp" || ext == ".chd";
}

std::string region_of(const std::string& id) {
  if (id.size() < 4) return "";
  switch (std::toupper(static_cast<unsigned char>(id[2]))) {
    case 'U': return "USA";
    case 'E': return "Europe";
    case 'J': return "Japan";
    case 'K': return "Korea";
    case 'A': return "Asia";
    default: return "";
  }
}

bool read_disc(const fs::path& file, DiscInfo& out) {
  out = {};
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  Bytes head;
  if (!read_at(in, 0, head, 16)) return false;
  const std::string ext = lower(file.extension().string());
  Bytes sfo;
  if (std::memcmp(head.data(), "\0PBP", 4) == 0) {
    // EBOOT.PBP: eight offsets after the version; the first file is PARAM.SFO.
    Bytes table;
    if (!read_at(in, 8, table, 8)) return false;
    const uint32_t from = u32(&table[0]), to = u32(&table[4]);
    if (to <= from || to - from > 64 * 1024 || !read_at(in, from, sfo, to - from)) return false;
  } else if (std::memcmp(head.data(), "MComprHD", 8) == 0) {
    return ext == ".chd" && chd_is_dvd(in);  // a UMD as a CHD: recognised, read by the core
  } else {
    SectorReader sectors;
    if (std::memcmp(head.data(), "CISO", 4) == 0) {
      if (!cso_reader(in, sectors)) return false;
    } else {
      sectors = iso_reader(in);
    }
    if (!read_iso_file(sectors, "PSP_GAME/PARAM.SFO", sfo)) return false;
  }
  read_sfo(sfo, out);
  out.disc_id.erase(std::remove(out.disc_id.begin(), out.disc_id.end(), '-'), out.disc_id.end());
  out.region = region_of(out.disc_id);
  return !out.disc_id.empty() || !out.title.empty();
}

}  // namespace p64::psp
