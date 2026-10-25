// n64save: inspect, import and convert N64 in-game saves.
//
//   n64save inspect <file-or-folder>...
//   n64save import-pj64 <Project64 folder> --library <N64 Saves folder> [--dry-run] [--replace-current] [--keep N]
//   n64save convert --from <format> --to <format> --name <base name> --out <folder> [--overwrite] <files>...
//   n64save rom-id <rom file>
//   n64save formats

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "savekit/formats.hpp"
#include "savekit/inspect.hpp"
#include "savekit/library.hpp"
#include "savekit/rom_header.hpp"

namespace fs = std::filesystem;
using namespace savekit;

namespace {

int usage() {
  std::cerr << "usage:\n"
               "  n64save inspect <file-or-folder>...\n"
               "  n64save import-pj64 <Project64 folder> --library <folder> [--dry-run] [--replace-current] [--keep N]\n"
               "  n64save convert --from <format> --to <format> --name <base name> --out <folder> [--overwrite] "
               "<files>...\n"
               "  n64save rom-id <rom file>\n"
               "  n64save formats\n";
  return 2;
}

int cmd_inspect(const std::vector<std::string>& args) {
  if (args.empty()) return usage();
  for (const auto& a : args) {
    auto reports = inspect_path(a);
    if (reports.empty()) std::cout << a << ": no save files or save states found\n";
    for (const auto& r : reports) {
      std::cout << r.path.string() << "\n  " << r.kind << ", " << r.size << " bytes\n  sha256 " << r.sha256 << "\n";
      for (const auto& f : r.facts) std::cout << "  - " << f << "\n";
      std::cout << "\n";
    }
  }
  return 0;
}

int cmd_import(const std::vector<std::string>& args) {
  ImportOptions o;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--library" && i + 1 < args.size()) {
      o.library = args[++i];
    } else if (args[i] == "--keep" && i + 1 < args.size()) {
      o.history_limit = std::stoul(args[++i]);
    } else if (args[i] == "--dry-run") {
      o.dry_run = true;
    } else if (args[i] == "--replace-current") {
      o.replace_current = true;
    } else if (o.source.empty()) {
      o.source = args[i];
    } else {
      return usage();
    }
  }
  if (o.source.empty() || o.library.empty() || o.history_limit == 0) return usage();
  auto report = import_project64(o);
  for (const auto& l : report.lines) std::cout << l << "\n";
  return report.conflicts.empty() ? 0 : 3;
}

int cmd_convert(const std::vector<std::string>& args) {
  std::optional<Format> from, to;
  std::string name;
  fs::path out;
  bool overwrite = false;
  std::vector<fs::path> inputs;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--from" && i + 1 < args.size()) {
      from = parse_format(args[++i]);
      if (!from) {
        std::cerr << "unknown format: " << args[i] << "\n";
        return 2;
      }
    } else if (args[i] == "--to" && i + 1 < args.size()) {
      to = parse_format(args[++i]);
      if (!to) {
        std::cerr << "unknown format: " << args[i] << "\n";
        return 2;
      }
    } else if (args[i] == "--name" && i + 1 < args.size()) {
      name = args[++i];
    } else if (args[i] == "--out" && i + 1 < args.size()) {
      out = args[++i];
    } else if (args[i] == "--overwrite") {
      overwrite = true;
    } else {
      inputs.emplace_back(args[i]);
    }
  }
  if (!from || !to || out.empty() || inputs.empty() || (name.empty() && *to != Format::Canonical)) return usage();

  FileSet files;
  for (const auto& p : inputs) {
    auto role = match_role(*from, p.filename().string());
    if (!role) {
      std::cerr << p.string() << ": not a " << format_id(*from) << " save file name\n";
      return 2;
    }
    if (files.count(*role)) {
      std::cerr << p.string() << ": more than one input is a '" << *role << "' file\n";
      return 2;
    }
    files[*role] = read_file(p);
  }
  auto decoded = decode(*from, files);
  for (const auto& n : decoded.notes) std::cout << "note: " << n << "\n";
  const auto encoded = encode(*to, decoded.save);
  if (encoded.empty()) std::cout << "nothing to write: the save holds no data\n";

  const auto now = std::chrono::system_clock::now();
  for (const auto& [role, data] : encoded) {
    const fs::path target = out / (*to == Format::Canonical ? role : name + role);
    if (fs::exists(target)) {
      if (!overwrite) {
        std::cerr << target.string() << " exists; pass --overwrite to replace it (it is backed up first)\n";
        return 3;
      }
      const fs::path backup = back_up_file(target, now);
      std::cout << "backed up " << target.string() << " -> " << backup.filename().string() << "\n";
      fs::remove(target);
    }
    write_new_file(target, data);
    std::cout << "wrote " << target.string() << " (" << data.size() << " bytes)\n";
  }
  return 0;
}

int cmd_rom_id(const std::vector<std::string>& args) {
  if (args.size() != 1) return usage();
  std::ifstream in(args[0], std::ios::binary);
  Bytes head(0x40);
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  auto h = parse_rom_header(head);
  if (!in || !h) {
    std::cerr << args[0] << ": not an N64 ROM\n";
    return 1;
  }
  const char* order = h->byte_order == RomByteOrder::BigEndian    ? "big-endian (.z64)"
                      : h->byte_order == RomByteOrder::ByteSwapped ? "byte-swapped (.v64)"
                                                                   : "little-endian (.n64)";
  std::cout << "game id:       " << h->game_id() << "\n"
            << "internal name: " << h->internal_name << "\n"
            << "game code:     " << h->game_code << "\n"
            << "version:       " << int(h->version) << "\n"
            << "dump order:    " << order << "\n";
  return 0;
}

int cmd_formats() {
  for (const auto& f : all_formats()) {
    std::cout << f.id << "\n  " << f.name << "\n  files:";
    for (const auto& r : file_roles(f.format)) std::cout << " " << r;
    std::cout << "\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  const std::string cmd = argv[1];
  const std::vector<std::string> args(argv + 2, argv + argc);
  try {
    if (cmd == "inspect") return cmd_inspect(args);
    if (cmd == "import-pj64") return cmd_import(args);
    if (cmd == "convert") return cmd_convert(args);
    if (cmd == "rom-id") return cmd_rom_id(args);
    if (cmd == "formats") return cmd_formats();
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
  return usage();
}
