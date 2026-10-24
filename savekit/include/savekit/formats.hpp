#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "savekit/save_set.hpp"

namespace savekit {

// On-disk save layouts we read and write. See docs/decisions/0003-save-formats.md for the
// evidence behind each layout and what is still unverified.
enum class Format {
  // Our own master copy: one file per memory, N64 byte order, full size.
  Canonical,
  // Project64 1.6 up to current releases: .eep, .sra, .fla, <name>_Cont_N.mpk.
  // SRAM and FlashRAM are stored word-swapped and files may be shorter than the chip.
  Project64,
  // Standalone mupen64plus (also the core inside RMG and simple64): .eep, .sra, .fla, .mpk.
  Mupen64Plus,
  // RetroArch with the Mupen64Plus-Next core: one combined .srm file.
  RetroArchMupen64PlusNext,
};

struct FormatInfo {
  Format format;
  std::string_view id;
  std::string_view name;
};

std::span<const FormatInfo> all_formats();
std::optional<Format> parse_format(std::string_view id);
std::string_view format_id(Format format);

// The files of one save in a given format, keyed by file role. For emulator formats the key
// is the suffix appended to the emulator's base name (".sra", "_Cont_1.mpk"); for Canonical
// it is the whole file name ("sram.bin").
using FileSet = std::map<std::string, Bytes>;

// The length of each file as it was found. Re-encoding with the shape reproduces the
// original files byte for byte, including Project64's short files. Stored in info.json.
using Shape = std::map<std::string, std::size_t>;

struct Decoded {
  SaveSet save;
  Shape shape;
  std::vector<std::string> notes;
};

class FormatError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

// File roles the format uses, in a stable order.
std::vector<std::string> file_roles(Format format);

// Returns the role a file name belongs to (by suffix), if any.
std::optional<std::string> match_role(Format format, std::string_view file_name);

Decoded decode(Format format, const FileSet& files);

// Without a shape, writes a full-size file for every role whose memories are present.
// With a shape, writes exactly the roles in the shape at exactly those lengths.
FileSet encode(Format format, const SaveSet& save, const Shape* shape = nullptr);

}  // namespace savekit
