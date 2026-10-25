#include "savekit/library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <stdexcept>

#include "savekit/bytes.hpp"
#include "savekit/formats.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::system_clock;

namespace savekit {
namespace {

struct Snapshot {
  fs::path folder;
  std::string label;
  FileSet files;
  std::map<std::string, fs::path> paths;
  Clock::time_point modified{};
  std::string id;
  Decoded decoded;
};

struct GameScan {
  std::vector<Snapshot> snapshots;
  std::size_t states = 0;
  std::vector<fs::path> unrecognised;
};

std::string iso_utc(Clock::time_point t) {
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(t));
}

std::string stamp(Clock::time_point t) {
  return std::format("{:%Y%m%dT%H%M%SZ}", std::chrono::floor<std::chrono::seconds>(t));
}

Clock::time_point modified_time(const fs::path& p) {
  return std::chrono::clock_cast<Clock>(fs::last_write_time(p));
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Identifies a set of save files by content, so importing the same save twice is a no-op.
std::string snapshot_id(const FileSet& files) {
  Bytes all;
  for (const auto& [role, data] : files) {
    all.insert(all.end(), role.begin(), role.end());
    all.push_back(0);
    for (int i = 0; i < 8; ++i) all.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(data.size()) >> (8 * i)));
    all.insert(all.end(), data.begin(), data.end());
  }
  return sha256_hex(all).substr(0, 12);
}

std::string json_string(const std::string& s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out + "\"";
}

// A folder name that is valid on Windows, macOS and Linux.
std::string safe_name(const std::string& s) {
  std::string out;
  for (unsigned char c : s) {
    out.push_back(c < 0x20 || std::string_view("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string_view::npos
                      ? '_'
                      : static_cast<char>(c));
  }
  while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
  return out.empty() ? "_" : out;
}

bool is_within(const fs::path& child, const fs::path& parent) {
  const fs::path c = fs::weakly_canonical(child), p = fs::weakly_canonical(parent);
  auto [pi, ci] = std::mismatch(p.begin(), p.end(), c.begin(), c.end());
  return pi == p.end();
}

// "HARVESTMOON64.pj0.zip" -> "HARVESTMOON64"
std::optional<std::string> state_base(const std::string& name) {
  std::string n = name;
  if (lower(n).ends_with(".zip")) n.resize(n.size() - 4);
  auto dot = n.rfind('.');
  if (dot == std::string::npos) return std::nullopt;
  std::string ext = lower(n.substr(dot + 1));
  if (ext == "pj" || (ext.size() == 3 && ext.starts_with("pj") && std::isdigit(static_cast<unsigned char>(ext[2])))) {
    return n.substr(0, dot);
  }
  return std::nullopt;
}

void write_text_new(const fs::path& path, const std::string& text) {
  write_new_file(path, Bytes(text.begin(), text.end()));
}

std::string shape_json(const Shape& shape) {
  std::string out = "{";
  for (const auto& [role, size] : shape) {
    out += (out.size() > 1 ? ", " : "") + json_string(role) + ": " + std::to_string(size);
  }
  return out + "}";
}

// Writes one save version into `dir`, which must not exist yet: canonical/ (always), raw/ (the
// files exactly as found, when there are any), entry.json and id.txt. Builds in a sibling
// ".partial" folder and renames, so a crash never leaves half an entry.
void write_entry(const fs::path& dir, const std::string& id, const SaveSet& save,
                 const std::vector<std::pair<std::string, const Bytes*>>& raw, const std::string& details) {
  if (fs::exists(dir)) throw std::runtime_error("refusing to overwrite " + dir.string());
  fs::path partial = dir;
  partial += ".partial";
  fs::remove_all(partial);

  std::string files_json;
  auto add = [&](const std::string& rel, const Bytes& data) {
    write_new_file(partial / fs::path(rel), data);
    files_json += (files_json.empty() ? "" : ",\n    ") + json_string(rel) + ": " + json_string(sha256_hex(data));
  };
  for (const auto& [name, data] : raw) add("raw/" + name, *data);
  for (const auto& [role, data] : encode(Format::Canonical, save)) add("canonical/" + role, data);

  std::string entry = "{\n  \"id\": " + json_string(id) + ",\n" + details;
  entry += "  \"files\": {\n    " + files_json + "\n  }\n}\n";
  write_text_new(partial / "entry.json", entry);
  write_text_new(partial / "id.txt", id + "\n");
  fs::rename(partial, dir);
}

void write_snapshot(const fs::path& dir, const Snapshot& s) {
  std::vector<std::pair<std::string, const Bytes*>> raw;
  for (const auto& [role, data] : s.files) raw.emplace_back(s.paths.at(role).filename().string(), &data);
  std::string details;
  details += "  \"source\": \"imported save file\",\n";
  details += "  \"label\": " + json_string(s.label) + ",\n";
  details += "  \"saved\": " + json_string(iso_utc(s.modified)) + ",\n";
  details += "  \"shape\": " + shape_json(s.decoded.shape) + ",\n";
  write_entry(dir, s.id, s.decoded.save, raw, details);
}

std::vector<fs::path> history_entries(const fs::path& history) {
  std::vector<fs::path> out;
  if (!fs::is_directory(history)) return out;
  for (const auto& e : fs::directory_iterator(history)) {
    if (e.is_directory() && e.path().extension() != ".partial") out.push_back(e.path());
  }
  std::sort(out.begin(), out.end());  // Names start with a UTC timestamp.
  return out;
}

bool history_has(const fs::path& history, const std::string& id) {
  for (const auto& p : history_entries(history)) {
    if (p.filename().string().find("__" + id) != std::string::npos) return true;
  }
  return false;
}

// The timestamp of the import (not a backup) in history with this id, if there is one.
std::optional<std::string> imported_stamp(const fs::path& history, const std::string& id) {
  for (const auto& p : history_entries(history)) {
    const std::string name = p.filename().string();
    if (name.ends_with("__" + id)) return name.substr(0, name.size() - id.size() - 2);
  }
  return std::nullopt;
}

std::string read_id(const fs::path& dir) {
  try {
    Bytes b = read_file(dir / "id.txt");
    std::string s(b.begin(), b.end());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
  } catch (const std::exception&) {
    return "unknown";
  }
}

std::map<std::string, GameScan> scan(const fs::path& source, const fs::path& root) {
  std::map<std::string, GameScan> games;
  std::vector<fs::path> files;
  if (fs::is_directory(source)) {
    for (const auto& e : fs::recursive_directory_iterator(root)) {
      if (e.is_regular_file()) files.push_back(e.path());
    }
  } else {
    files.push_back(source);
  }
  std::sort(files.begin(), files.end());

  for (const auto& file : files) {
    const std::string name = file.filename().string();
    if (auto role = match_role(Format::Project64, name)) {
      const std::string base = name.substr(0, name.size() - role->size());
      const fs::path folder = fs::is_directory(source) ? fs::relative(file.parent_path(), root) : fs::path(".");
      auto& snaps = games[base].snapshots;
      auto it = std::find_if(snaps.begin(), snaps.end(), [&](const Snapshot& s) { return s.folder == folder; });
      if (it == snaps.end()) {
        Snapshot s;
        s.folder = folder;
        s.label = folder == "." ? "top level" : folder.generic_string();
        snaps.push_back(std::move(s));
        it = snaps.end() - 1;
      }
      it->files[*role] = read_file(file);
      it->paths[*role] = file;
      it->modified = std::max(it->modified, modified_time(file));
    } else if (auto base = state_base(name)) {
      games[*base].states++;
    } else if (lower(file.extension().string()) == ".mpk") {
      games[file.stem().string()].unrecognised.push_back(file);
    }
  }
  return games;
}

void prune(const fs::path& history, std::size_t limit, bool dry_run, std::vector<std::string>& lines) {
  auto entries = history_entries(history);
  for (std::size_t i = 0; i + limit < entries.size(); ++i) {
    lines.push_back("  history is over " + std::to_string(limit) + " versions; removing oldest: " +
                    entries[i].filename().string());
    if (!dry_run) fs::remove_all(entries[i]);
  }
}

// `imported` is the snapshot now in current/, or null when current/ holds a save from elsewhere.
struct CurrentInfo {
  std::string source;
  std::string label;
  Clock::time_point saved;
  const Shape* shape = nullptr;
};

// Rewrites info.json from what is on disk. `current` describes the save now in current/, when known.
void write_info(const fs::path& game_dir, const std::string& base, const CurrentInfo* current, Clock::time_point now) {
  std::string types;
  if (fs::is_directory(game_dir / "current" / "canonical")) {
    std::vector<std::string> names;
    for (const auto& e : fs::directory_iterator(game_dir / "current" / "canonical")) {
      names.push_back(e.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    for (const auto& n : names) types += (types.empty() ? "" : ", ") + json_string(n);
  }
  std::string history;
  for (const auto& p : history_entries(game_dir / "history")) {
    history += (history.empty() ? "" : ",\n    ") + json_string(p.filename().string());
  }
  const char* device = std::getenv("COMPUTERNAME");
  if (!device) device = std::getenv("HOSTNAME");

  std::string j = "{\n";
  j += "  \"schema\": 2,\n";
  j += "  \"game\": {\n";
  j += "    \"internal_name\": " + json_string(base) + "\n";
  j += "  },\n";
  j += "  \"save_types\": [" + types + "],\n";
  j += "  \"current\": {\n";
  j += "    \"id\": " + json_string(read_id(game_dir / "current"));
  if (current) {
    j += ",\n    \"source\": " + json_string(current->source) + ",\n";
    j += "    \"label\": " + json_string(current->label) + ",\n";
    j += "    \"saved\": " + json_string(iso_utc(current->saved));
    if (current->shape) j += ",\n    \"shape\": " + shape_json(*current->shape);
  }
  j += "\n  },\n";
  j += "  \"history\": [\n    " + history + "\n  ],\n";
  j += "  \"updated\": " + json_string(iso_utc(now)) + ",\n";
  j += "  \"device\": " + json_string(device ? device : "unknown") + "\n";
  j += "}\n";

  fs::path partial = game_dir / "info.json.partial";
  fs::remove(partial);
  write_text_new(partial, j);
  const fs::path target = game_dir / "info.json";
  std::error_code ec;
  fs::rename(partial, target, ec);
  if (ec) {
    // Some platforms refuse to rename over an existing file. info.json is regenerated from what
    // is on disk every time, so replacing it in two steps loses nothing.
    fs::remove(target);
    fs::rename(partial, target);
  }
}

}  // namespace

ImportReport import_project64(const ImportOptions& o) {
  if (!fs::exists(o.source)) throw std::runtime_error("source not found: " + o.source.string());
  const fs::path root = fs::is_directory(o.source) && fs::is_directory(o.source / "Save") ? o.source / "Save" : o.source;
  // Compare against the folder the user named, not just its Save/ subfolder: the library must
  // never be created anywhere inside the emulator's own folder.
  const fs::path source_dir = fs::is_directory(o.source) ? o.source : o.source.parent_path();
  if (is_within(o.library, source_dir) || is_within(source_dir, o.library)) {
    throw std::runtime_error("the library folder and the source folder must not contain each other");
  }

  ImportReport report;
  auto& lines = report.lines;
  const std::string dry = o.dry_run ? "(dry run) " : "";
  auto games = scan(o.source, root);
  if (games.empty()) lines.push_back("no Project64 saves or save states found under " + root.string());

  for (auto& [base, game] : games) {
    lines.push_back(base + ":");
    for (const auto& p : game.unrecognised) {
      lines.push_back("  not imported: " + p.filename().string() +
                      " (Controller Pak file name not recognised; expected <name>_Cont_N.mpk)");
    }
    if (game.states) {
      lines.push_back("  " + std::to_string(game.states) +
                      " Project64 save state(s) found; these will not load in this app and are not copied");
    }

    std::vector<Snapshot*> valid;
    for (auto& s : game.snapshots) {
      try {
        s.decoded = decode(Format::Project64, s.files);
        s.id = snapshot_id(s.files);
        valid.push_back(&s);
        for (const auto& n : s.decoded.notes) lines.push_back("  note (" + s.label + "): " + n);
      } catch (const FormatError& e) {
        lines.push_back("  skipped (" + s.label + "): " + e.what());
      }
    }
    if (valid.empty()) {
      lines.push_back("  no in-game save to import");
      continue;
    }
    std::sort(valid.begin(), valid.end(), [](const Snapshot* a, const Snapshot* b) { return a->modified < b->modified; });

    const fs::path game_dir = o.library / safe_name(base);
    const fs::path history = game_dir / "history";
    for (const Snapshot* s : valid) {
      if (history_has(history, s->id)) {
        lines.push_back("  already in history: " + s->label + " (" + s->id + ")");
        continue;
      }
      const fs::path dir = history / (stamp(s->modified) + "__" + s->id);
      lines.push_back("  " + dry + "added to history: " + s->label + ", saved " + iso_utc(s->modified) + " -> " +
                      dir.filename().string());
      if (!o.dry_run) write_snapshot(dir, *s);
    }

    const Snapshot& newest = *valid.back();
    const fs::path current = game_dir / "current";
    bool current_updated = false;
    if (!fs::exists(current)) {
      lines.push_back("  " + dry + "current save: " + newest.label + " (newest, saved " + iso_utc(newest.modified) + ")");
      if (!o.dry_run) write_snapshot(current, newest);
      current_updated = true;
    } else if (const std::string existing = read_id(current); existing == newest.id) {
      lines.push_back("  current save already matches the newest import");
    } else if (const auto existing_stamp = imported_stamp(history, existing);
               existing_stamp && stamp(newest.modified) <= *existing_stamp) {
      // The newest save in Project64 is older than current (for example the newer file was
      // deleted or moved there). Never move current backwards.
      lines.push_back("  current save is newer than anything in Project64 (saved " + *existing_stamp +
                      "); left as is");
    } else if (existing_stamp) {
      // current/ is an untouched earlier import and that save is already kept in history, so
      // only the Project64 side has changed since: moving current forward loses nothing.
      lines.push_back("  " + dry + "current save: " + newest.label + " (replaces an earlier import, kept in history)");
      if (!o.dry_run) {
        fs::path incoming = game_dir / "current.incoming";
        fs::remove_all(incoming);
        write_snapshot(incoming, newest);
        fs::remove_all(current);
        fs::rename(incoming, current);
      }
      current_updated = true;
    } else if (o.replace_current) {
      const fs::path backup = history / (stamp(o.now) + "__" + existing + "__replaced");
      lines.push_back("  " + dry + "backing up existing current save -> " + backup.filename().string());
      lines.push_back("  " + dry + "current save: " + newest.label);
      if (!o.dry_run) {
        fs::path incoming = game_dir / "current.incoming";
        fs::remove_all(incoming);
        write_snapshot(incoming, newest);
        fs::create_directories(history);
        fs::copy(current, backup, fs::copy_options::recursive);
        if (read_id(backup) != existing) throw std::runtime_error("backup verification failed: " + backup.string());
        fs::remove_all(current);
        fs::rename(incoming, current);
      }
      current_updated = true;
    } else {
      lines.push_back("  CONFLICT: the library already has a different current save (" + existing +
                      "). Nothing was overwritten; the imported save is in history. "
                      "Re-run with --replace-current to make it current (the existing one is backed up first)");
      report.conflicts.push_back(base);
    }

    prune(history, o.history_limit, o.dry_run, lines);
    if (!o.dry_run) {
      const bool newest_is_current = current_updated || read_id(current) == newest.id;
      const CurrentInfo info{"imported save file", newest.label, newest.modified, &newest.decoded.shape};
      write_info(game_dir, base, newest_is_current ? &info : nullptr, o.now);
    }
  }
  return report;
}

std::optional<SaveSet> load_current(const fs::path& library, const std::string& game) {
  const fs::path canonical = library / safe_name(game) / "current" / "canonical";
  if (!fs::is_directory(canonical)) return std::nullopt;
  FileSet files;
  for (const auto& e : fs::directory_iterator(canonical)) {
    if (auto role = match_role(Format::Canonical, e.path().filename().string())) files[*role] = read_file(e.path());
  }
  return decode(Format::Canonical, files).save;
}

StoreResult store_current(const StoreOptions& o) {
  const fs::path game_dir = o.library / safe_name(o.game);
  const fs::path history = game_dir / "history";
  const fs::path current = game_dir / "current";
  StoreResult r;
  r.id = snapshot_id(encode(Format::Canonical, o.save));
  if (fs::exists(current) && read_id(current) == r.id) return r;  // unchanged

  std::string details;
  details += "  \"source\": " + json_string(o.source) + ",\n";
  details += "  \"saved\": " + json_string(iso_utc(o.now)) + ",\n";
  write_entry(history / (stamp(o.now) + "__" + r.id), r.id, o.save, {}, details);

  fs::path incoming = game_dir / "current.incoming";
  fs::remove_all(incoming);
  write_entry(incoming, r.id, o.save, {}, details);
  if (fs::exists(current)) {
    const std::string old = read_id(current);
    if (!history_has(history, old)) {
      // The previous save is not in history (it was pruned, or came from elsewhere): keep it.
      const fs::path backup = history / (stamp(o.now) + "__" + old + "__replaced");
      if (!fs::exists(backup)) fs::copy(current, backup, fs::copy_options::recursive);
    }
    fs::remove_all(current);
  }
  fs::rename(incoming, current);
  r.changed = true;

  std::vector<std::string> ignored;
  prune(history, o.history_limit, false, ignored);
  const CurrentInfo info{o.source, o.source, o.now, nullptr};
  write_info(game_dir, o.game, &info, o.now);
  return r;
}

std::vector<std::string> library_games(const fs::path& library) {
  std::vector<std::string> games;
  if (!fs::is_directory(library)) return games;
  for (const auto& e : fs::directory_iterator(library)) {
    if (e.is_directory() && fs::is_directory(e.path() / "current" / "canonical")) {
      games.push_back(e.path().filename().string());
    }
  }
  std::sort(games.begin(), games.end());
  return games;
}

fs::path back_up_file(const fs::path& path, Clock::time_point now) {
  fs::path backup = path;
  backup += ".bak-" + stamp(now);
  fs::copy_file(path, backup);
  if (read_file(backup) != read_file(path)) throw std::runtime_error("backup verification failed: " + backup.string());
  return backup;
}

ExportReport export_current(const ExportOptions& o) {
  if (o.base_name.empty()) throw std::runtime_error("a file name is needed");
  if (o.base_name.find_first_of("<>:\"/\\|?*") != std::string::npos) {
    throw std::runtime_error("the file name cannot contain any of < > : \" / \\ | ? *");
  }
  const fs::path canonical = o.library / o.game / "current" / "canonical";
  if (!fs::is_directory(canonical)) throw std::runtime_error("no current save for " + o.game);

  FileSet files;
  for (const auto& e : fs::directory_iterator(canonical)) {
    if (auto role = match_role(Format::Canonical, e.path().filename().string())) files[*role] = read_file(e.path());
  }
  const FileSet encoded = encode(o.format, decode(Format::Canonical, files).save);

  ExportReport r;
  if (encoded.empty()) {
    r.lines.push_back(o.game + ": the save holds no data; nothing to export");
    return r;
  }
  std::vector<std::pair<fs::path, const Bytes*>> targets;
  for (const auto& [role, data] : encoded) {
    targets.emplace_back(o.out_dir / (o.format == Format::Canonical ? role : o.base_name + role), &data);
    if (fs::exists(targets.back().first)) r.existing.push_back(targets.back().first);
  }
  if (!r.existing.empty() && !o.replace_existing) {
    for (const auto& p : r.existing) r.lines.push_back("already exists, not replaced: " + p.string());
    return r;
  }
  for (const auto& [target, data] : targets) {
    if (fs::exists(target)) {
      const fs::path backup = back_up_file(target, o.now);
      r.lines.push_back("backed up the old file as " + backup.filename().string());
      fs::remove(target);
    }
    write_new_file(target, *data);
    r.written.push_back(target);
    r.lines.push_back("wrote " + target.string() + " (" + std::to_string(data->size()) + " bytes)");
  }
  return r;
}

}  // namespace savekit
