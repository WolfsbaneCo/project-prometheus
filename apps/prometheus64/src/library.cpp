#include "library.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>

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

std::string display_title(const fs::path& file) {
  std::string t = file.stem().string();
  std::replace(t.begin(), t.end(), '_', ' ');
  return t;
}

}  // namespace

bool is_game_file(const fs::path& file) {
  const std::string ext = lower(file.extension().string());
  return ext == ".z64" || ext == ".n64" || ext == ".v64";
}

bool read_game(const fs::path& file, Game& out) {
  std::ifstream in(file, std::ios::binary);
  savekit::Bytes head(0x40);
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  if (!in) return false;
  auto h = savekit::parse_rom_header(head);
  if (!h) return false;
  out.path = file;
  out.title = display_title(file);
  out.internal_name = h->internal_name.empty() ? out.title : h->internal_name;
  out.id = h->game_id();
  out.region = region_name(h->country);
  return true;
}

std::vector<Game> scan_games(const fs::path& folder) {
  std::vector<Game> games;
  std::error_code ec;
  if (folder.empty() || !fs::is_directory(folder, ec)) return games;
  auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
  for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it.depth() > 2) it.disable_recursion_pending();
    if (!it->is_regular_file(ec) || !is_game_file(it->path())) continue;
    Game g;
    if (read_game(it->path(), g)) games.push_back(std::move(g));
  }
  std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return lower(a.title) < lower(b.title); });
  return games;
}

}  // namespace p64
