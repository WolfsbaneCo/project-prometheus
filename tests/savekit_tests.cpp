// Tests for the inspector, importer, ROM header parsing and hashing. Synthetic data only.

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "check.hpp"
#include "savekit/formats.hpp"
#include "savekit/inspect.hpp"
#include "savekit/library.hpp"
#include "savekit/rom_header.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;
using namespace savekit;
using namespace std::chrono_literals;

namespace {

// A fresh, empty folder per test, removed afterwards.
struct TempDir {
  fs::path path;
  explicit TempDir(const std::string& name) {
    path = fs::temp_directory_path() / ("prometheus64-test-" + name + "-" +
                                        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

void put(const fs::path& p, const Bytes& data, fs::file_time_type when) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  fs::last_write_time(p, when);
}

// A Project64-style SRAM file: synthetic text, word-swapped, cut short like Project64 1.6 does.
Bytes pj64_sram(const char* text, std::size_t length) {
  Bytes canonical(kSramSize, 0xFF);
  std::memcpy(canonical.data(), text, std::strlen(text));
  Bytes file = encode(Format::Project64, {{{Memory::Sram, canonical}}}).at(".sra");
  file.resize(length);
  return file;
}

// Snapshot of every file's bytes and modification time, to prove a source was not touched.
std::map<fs::path, std::pair<Bytes, fs::file_time_type>> fingerprint(const fs::path& root) {
  std::map<fs::path, std::pair<Bytes, fs::file_time_type>> out;
  for (const auto& e : fs::recursive_directory_iterator(root)) {
    if (e.is_regular_file()) out[e.path()] = {read_file(e.path()), fs::last_write_time(e.path())};
  }
  return out;
}

std::size_t count_dirs(const fs::path& p) {
  if (!fs::exists(p)) return 0;
  std::size_t n = 0;
  for (const auto& e : fs::directory_iterator(p)) n += e.is_directory() ? 1 : 0;
  return n;
}

std::string text_of(const fs::path& p) {
  Bytes b = read_file(p);
  return std::string(b.begin(), b.end());
}

struct FakeProject64 {
  TempDir dir{"pj64"};
  fs::path root = dir.path / "Project 64 1.6";
  fs::file_time_type t0 = fs::file_time_type::clock::now() - 1000h;

  FakeProject64() {
    put(root / "Save" / "Spring 15" / "TESTGAME.sra", pj64_sram("SYNTHETIC SAVE OLDER", 4096), t0);
    put(root / "Save" / "TESTGAME.sra", pj64_sram("SYNTHETIC SAVE NEWER", 4096), t0 + 24h);
    put(root / "Save" / "TESTGAME.pj0.zip", Bytes{'P', 'K', 5, 6}, t0);
    put(root / "Save" / "TESTGAME.pj.zip", Bytes{'P', 'K', 5, 6}, t0);
    put(root / "Project64.exe", Bytes{'M', 'Z'}, t0);
  }
};

}  // namespace

TEST_CASE("sha256 matches published test vectors") {
  CHECK(sha256_hex({}) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_hex({'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string long_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(sha256_hex(Bytes(long_msg.begin(), long_msg.end())) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("ROM header reads the same in all three dump byte orders") {
  Bytes z64(0x40, 0);
  const std::uint8_t magic[] = {0x80, 0x37, 0x12, 0x40};
  std::memcpy(z64.data(), magic, 4);
  const std::uint8_t crc[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
  std::memcpy(z64.data() + 0x10, crc, 8);
  std::memcpy(z64.data() + 0x20, "SYNTHETIC TEST      ", 20);
  std::memcpy(z64.data() + 0x3B, "NZZE", 4);
  z64[0x3F] = 1;

  Bytes v64 = z64;
  for (std::size_t i = 0; i < v64.size(); i += 2) std::swap(v64[i], v64[i + 1]);
  Bytes n64 = swap_words(z64);

  for (const Bytes* dump : {&z64, &v64, &n64}) {
    auto h = parse_rom_header(*dump);
    CHECK(h.has_value());
    if (!h) continue;
    CHECK(h->game_id() == "12345678-9ABCDEF0-C:45");
    CHECK(h->internal_name == "SYNTHETIC TEST");
    CHECK(h->game_code == "NZZE");
    CHECK(h->version == 1);
  }
  CHECK(parse_rom_header(z64)->byte_order == RomByteOrder::BigEndian);
  CHECK(parse_rom_header(v64)->byte_order == RomByteOrder::ByteSwapped);
  CHECK(parse_rom_header(n64)->byte_order == RomByteOrder::LittleEndian);
  CHECK(!parse_rom_header(Bytes(0x40, 0)).has_value());
  CHECK(!parse_rom_header(Bytes(0x10, 0)).has_value());
}

TEST_CASE("inspector reports a short word-swapped SRAM and does not touch it") {
  TempDir dir("inspect");
  const fs::path sra = dir.path / "TESTGAME.sra";
  put(sra, pj64_sram("PACKED EVERCART TEST SAVE", 4096), fs::file_time_type::clock::now() - 10h);
  const auto before = fingerprint(dir.path);

  const auto reports = inspect_path(dir.path);
  CHECK(reports.size() == 1);
  std::string all;
  for (const auto& f : reports.at(0).facts) all += f + "\n";
  CHECK(reports.at(0).kind == "in-game save: SRAM");
  CHECK(all.find("file is 4 KB") != std::string::npos);
  CHECK(all.find("\"PACKED EVERCART TEST SAVE\"") != std::string::npos);
  CHECK(all.find("byte order: word-swapped") != std::string::npos);
  CHECK(fingerprint(dir.path) == before);
}

TEST_CASE("import copies saves, never touches the source, and keeps history") {
  FakeProject64 pj;
  TempDir lib("library");
  const auto before = fingerprint(pj.root);

  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  auto report = import_project64(o);
  CHECK(report.conflicts.empty());
  CHECK(fingerprint(pj.root) == before);

  const fs::path game = o.library / "TESTGAME";
  CHECK(read_file(game / "current" / "raw" / "TESTGAME.sra") == read_file(pj.root / "Save" / "TESTGAME.sra"));
  const Bytes canonical = read_file(game / "current" / "canonical" / "sram.bin");
  CHECK(canonical.size() == kSramSize);
  CHECK(std::memcmp(canonical.data(), "SYNTHETIC SAVE NEWER", 20) == 0);
  CHECK(count_dirs(game / "history") == 2);
  const std::string info = text_of(game / "info.json");
  CHECK(info.find("\"found\": 2") != std::string::npos);
  CHECK(info.find("\"shape\": {\".sra\": 4096}") != std::string::npos);
  CHECK(info.find("\"save_types\": [\"sram\"]") != std::string::npos);

  // Importing again changes nothing.
  report = import_project64(o);
  CHECK(count_dirs(game / "history") == 2);
  CHECK(fingerprint(pj.root) == before);
}

TEST_CASE("dry run writes nothing") {
  FakeProject64 pj;
  TempDir lib("dry");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  o.dry_run = true;
  auto report = import_project64(o);
  CHECK(!report.lines.empty());
  CHECK(!fs::exists(o.library));
}

TEST_CASE("re-import after more Project64 play moves current forward") {
  FakeProject64 pj;
  TempDir lib("forward");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  import_project64(o);

  put(pj.root / "Save" / "TESTGAME.sra", pj64_sram("SYNTHETIC SAVE NEWEST", 4096), pj.t0 + 48h);
  auto report = import_project64(o);
  CHECK(report.conflicts.empty());
  const fs::path game = o.library / "TESTGAME";
  CHECK(count_dirs(game / "history") == 3);
  CHECK(std::memcmp(read_file(game / "current" / "canonical" / "sram.bin").data(), "SYNTHETIC SAVE NEWEST", 21) == 0);
}

TEST_CASE("a library save from elsewhere is never silently replaced") {
  FakeProject64 pj;
  TempDir lib("conflict");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  import_project64(o);

  // Simulate the new app having saved a different current copy since the import.
  const fs::path current = o.library / "TESTGAME" / "current";
  // Binary mode: a text-mode stream would write "\r\n" on Windows.
  put(current / "id.txt", Bytes{'p', 'l', 'a', 'y', 'e', 'd', 'i', 'n', 'a', 'p', 'p', '1', '\n'},
      fs::file_time_type::clock::now());
  const Bytes app_save = read_file(current / "canonical" / "sram.bin");
  put(pj.root / "Save" / "TESTGAME.sra", pj64_sram("SYNTHETIC SAVE NEWEST", 4096), pj.t0 + 48h);

  auto report = import_project64(o);
  CHECK(report.conflicts.size() == 1);
  CHECK(text_of(current / "id.txt") == "playedinapp1\n");
  CHECK(read_file(current / "canonical" / "sram.bin") == app_save);

  o.replace_current = true;
  report = import_project64(o);
  CHECK(report.conflicts.empty());
  bool backed_up = false;
  for (const auto& e : fs::directory_iterator(o.library / "TESTGAME" / "history")) {
    if (e.path().filename().string().ends_with("__playedinapp1__replaced")) backed_up = true;
  }
  CHECK(backed_up);
  CHECK(std::memcmp(read_file(current / "canonical" / "sram.bin").data(), "SYNTHETIC SAVE NEWEST", 21) == 0);
}

TEST_CASE("history keeps only the newest N versions") {
  FakeProject64 pj;
  put(pj.root / "Save" / "Older" / "TESTGAME.sra", pj64_sram("SYNTHETIC SAVE OLDEST", 4096), pj.t0 - 24h);
  TempDir lib("prune");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  o.history_limit = 2;
  import_project64(o);
  const fs::path history = o.library / "TESTGAME" / "history";
  CHECK(count_dirs(history) == 2);
  for (const auto& e : fs::directory_iterator(history)) {
    CHECK(text_of(e.path() / "entry.json").find("\"label\": \"Older\"") == std::string::npos);
  }
}

TEST_CASE("library anywhere inside the Project64 folder is refused") {
  FakeProject64 pj;
  ImportOptions o;
  o.source = pj.root;
  o.library = pj.root / "N64 Saves";  // Next to Save/, not inside it.
  CHECK_THROWS(import_project64(o));
  CHECK(!fs::exists(o.library));
}

TEST_CASE("current never moves back to an older save") {
  FakeProject64 pj;
  TempDir lib("backwards");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  import_project64(o);

  // The newer save disappears from Project64; only the older "Spring 15" copy is left.
  fs::remove(pj.root / "Save" / "TESTGAME.sra");
  auto report = import_project64(o);
  CHECK(report.conflicts.empty());
  CHECK(std::memcmp(read_file(o.library / "TESTGAME" / "current" / "canonical" / "sram.bin").data(),
                    "SYNTHETIC SAVE NEWER", 20) == 0);
}

TEST_CASE("export writes the current save in another format and never silently replaces") {
  FakeProject64 pj;
  TempDir lib("export");
  ImportOptions o;
  o.source = pj.root;
  o.library = lib.path / "N64 Saves";
  import_project64(o);
  CHECK(library_games(o.library) == std::vector<std::string>{"TESTGAME"});

  ExportOptions e;
  e.library = o.library;
  e.game = "TESTGAME";
  e.format = Format::RetroArchMupen64PlusNext;
  e.out_dir = lib.path / "out";
  e.base_name = "Test Game (USA)";
  auto r = export_current(e);
  const fs::path srm = e.out_dir / "Test Game (USA).srm";
  CHECK(r.written == std::vector<fs::path>{srm});
  const Bytes data = read_file(srm);
  CHECK(data.size() == 0x48800);
  const Bytes expected = pj64_sram("SYNTHETIC SAVE NEWER", 4096);
  CHECK(std::memcmp(data.data() + 0x20800, expected.data(), expected.size()) == 0);

  // A second export into the same place writes nothing unless asked to replace.
  put(srm, Bytes{'o', 'l', 'd'}, fs::file_time_type::clock::now());
  r = export_current(e);
  CHECK(r.written.empty());
  CHECK(r.existing == std::vector<fs::path>{srm});
  CHECK(read_file(srm) == (Bytes{'o', 'l', 'd'}));

  e.replace_existing = true;
  r = export_current(e);
  CHECK(r.written.size() == 1);
  CHECK(read_file(srm).size() == 0x48800);
  bool backed_up = false;
  for (const auto& f : fs::directory_iterator(e.out_dir)) {
    if (f.path().filename().string().starts_with("Test Game (USA).srm.bak-")) {
      backed_up = read_file(f.path()) == Bytes{'o', 'l', 'd'};
    }
  }
  CHECK(backed_up);

  e.base_name = "bad/name";
  CHECK_THROWS(export_current(e));
  e.base_name = "x";
  e.game = "NOSUCHGAME";
  CHECK_THROWS(export_current(e));
}

TEST_CASE("library inside the source folder is refused") {
  FakeProject64 pj;
  ImportOptions o;
  o.source = pj.root;
  o.library = pj.root / "Save" / "N64 Saves";
  CHECK_THROWS(import_project64(o));
  CHECK(!fs::exists(o.library));
}

int main() { return check::run_all(); }
