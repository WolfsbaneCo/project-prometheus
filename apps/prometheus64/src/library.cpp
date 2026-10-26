#include "library.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

#include "archive.hpp"
#include "savekit/rom_header.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

std::string region_name(unsigned char code) {
  switch (code) {
    case 'E': return "USA";
    case 'J': return "Japan";
    case 'P': case 'X': case 'Y': return "Europe";
    case 'D': return "Germany";
    case 'F': return "France";
    case 'I': return "Italy";
    case 'S': return "Spain";
    case 'U': return "Australia";
    case 'A': return "Asia";
    default: return "";
  }
}

// The largest N64 cartridge is 64 MB; anything bigger in an archive is not a game.
constexpr std::size_t kMaxGameSize = 64u << 20;

std::string display_title(const fs::path& file) {
  std::string t = file.stem().string();
  std::replace(t.begin(), t.end(), '_', ' ');
  return t;
}

bool has_game_extension(const fs::path& file) {
  const std::string ext = lower(file.extension().string());
  return ext == ".z64" || ext == ".n64" || ext == ".v64";
}

// Fills `out` from a game's first 64 bytes. Returns false when they are not an N64 ROM header.
bool describe(const savekit::Bytes& head, const fs::path& path, const std::string& entry, Game& out) {
  auto h = savekit::parse_rom_header(head);
  if (!h) return false;
  out.path = path;
  out.entry = entry;
  out.title = display_title(entry.empty() ? path : fs::path(std::u8string(entry.begin(), entry.end())).filename());
  out.internal_name = h->internal_name.empty() ? out.title : h->internal_name;
  out.id = h->game_id();
  out.region = region_name(h->country);
  return true;
}

}  // namespace

bool is_game_file(const fs::path& file) { return has_game_extension(file) || archive::is_archive(file); }

std::vector<Game> read_games(const fs::path& file, std::string* error) {
  std::vector<Game> games;
  if (error) error->clear();
  if (archive::is_archive(file)) {
    // Every file in the archive is checked by its header, whatever its name: archives often hold
    // a game under a .bin or .rom name, next to a readme.
    const std::string err = archive::scan(file, 0x40, [&](const archive::Entry& e, const std::vector<uint8_t>& head) {
      if (e.size > static_cast<int64_t>(kMaxGameSize)) return true;
      Game g;
      if (describe(head, file, e.name, g)) games.push_back(std::move(g));
      return true;
    });
    // One game per archive is the usual case: the archive's own name is then the better title
    // (the file inside is often called something like "rom.z64").
    if (games.size() == 1) games.front().title = display_title(file);
    if (error) *error = err;
    return games;
  }
  std::ifstream in(file, std::ios::binary);
  savekit::Bytes head(0x40);
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  Game g;
  if (in && describe(head, file, {}, g)) games.push_back(std::move(g));
  return games;
}

bool read_game(const fs::path& file, Game& out) {
  auto games = read_games(file);
  if (games.empty()) return false;
  out = std::move(games.front());
  return true;
}

std::string load_game_data(const Game& game, std::vector<uint8_t>& data) {
  if (!game.entry.empty()) return archive::extract(game.path, game.entry, kMaxGameSize, data);
  std::ifstream in(game.path, std::ios::binary);
  if (!in) return "could not open the game file";
  data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return {};
}

std::vector<Game> scan_games(const fs::path& folder) {
  std::vector<Game> games;
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return games;
  auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it.depth() > 2) it.disable_recursion_pending();
    if (!it->is_regular_file(ec) || !is_game_file(it->path())) continue;
    for (Game& g : read_games(it->path())) games.push_back(std::move(g));
  }
  std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return lower(a.title) < lower(b.title); });
  return games;
}

}  // namespace p64
