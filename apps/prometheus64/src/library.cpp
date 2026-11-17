#include "library.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "archive.hpp"
#include "psp_disc.hpp"
#include "savekit/rom_header.hpp"

namespace fs = std::filesystem;

namespace p64 {

const char* system_name(System s) {
  return s == System::GBA ? "GBA" : s == System::PSP ? "PSP" : s == System::GB ? "GB" : "N64";
}

std::string console_label(const Game& game) {
  if (game.system == System::GB && game.id.rfind("CGB-", 0) == 0) return "GBC";
  return system_name(game.system);
}

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

// The largest N64 cartridge is 64 MB (GBA: 32 MB, Game Boy: 8 MB); anything bigger in an archive is
// not a game.
constexpr std::size_t kMaxGameSize = 64u << 20;
// Bytes read from the start of each file to recognise it: the Game Boy header ends at 0x150 (the
// GBA's at 0xC0, the N64's at 0x40).
constexpr std::size_t kHeadSize = 0x150;

std::string display_title(const fs::path& file) {
  std::string t = file.stem().string();
  std::replace(t.begin(), t.end(), '_', ' ');
  return t;
}

bool has_game_extension(const fs::path& file) {
  const std::string ext = lower(file.extension().string());
  return ext == ".z64" || ext == ".n64" || ext == ".v64" || ext == ".gba" || ext == ".agb" || ext == ".gb" ||
         ext == ".gbc" || ext == ".cgb" || ext == ".sgb";
}

// A GBA cartridge header: title at 0xA0, game code at 0xAC, the fixed value 0x96 at 0xB2 and a
// checksum of 0xA0-0xBC at 0xBD. Checking both rules out other files that happen to fit.
bool describe_gba(const savekit::Bytes& head, Game& out) {
  if (head.size() < kHeadSize || head[0xB2] != 0x96) return false;
  unsigned sum = 0;
  for (std::size_t i = 0xA0; i <= 0xBC; ++i) sum += head[i];
  if (static_cast<uint8_t>(-static_cast<int>(sum) - 0x19) != head[0xBD]) return false;
  auto text = [&](std::size_t at, std::size_t n) {
    std::string s;
    for (std::size_t i = at; i < at + n && head[i]; ++i) s += static_cast<char>(head[i] >= 0x20 && head[i] < 0x7F ? head[i] : '_');
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
  };
  const std::string code = text(0xAC, 4);
  char tail[16];
  std::snprintf(tail, sizeof tail, "-%02X-%02X", head[0xBC], head[0xBD]);
  out.system = System::GBA;
  out.internal_name = text(0xA0, 12);
  out.id = "AGB-" + (code.empty() ? std::string("XXXX") : code) + tail;
  out.region = code.size() == 4 ? region_name(static_cast<unsigned char>(code[3])) : "";
  return true;
}

// A Game Boy cartridge header (0x100-0x14F): the Nintendo logo the console checks at 0x104, the
// title at 0x134, the Game Boy Color flag at 0x143 (0x80: also plays on a Game Boy, 0xC0: Color
// only), the destination at 0x14A (0: Japan) and a checksum of 0x134-0x14C at 0x14D. The logo and
// the checksum together rule out other files.
bool describe_gb(const savekit::Bytes& head, Game& out) {
  static const uint8_t kLogo[24] = {0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B, 0x03, 0x73, 0x00, 0x83,
                                    0x00, 0x0C, 0x00, 0x0D, 0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E};
  if (head.size() < 0x150 || !std::equal(std::begin(kLogo), std::end(kLogo), head.begin() + 0x104)) return false;
  uint8_t sum = 0;
  for (std::size_t i = 0x134; i <= 0x14C; ++i) sum = static_cast<uint8_t>(sum - head[i] - 1);
  if (sum != head[0x14D]) return false;
  const bool color = (head[0x143] & 0x80) != 0;
  // The title fills 16 bytes on the first games, 15 once the Color flag took the last one (later
  // games use 11 and a 4-letter code; it is kept, as it tells versions apart).
  std::string title;
  for (std::size_t i = 0x134; i < 0x134 + (color ? 15u : 16u) && head[i]; ++i) {
    const unsigned char c = head[i];
    title += static_cast<char>(c >= 0x20 && c < 0x7F && std::string("<>:\"/\\|?*").find(static_cast<char>(c)) == std::string::npos ? c : '_');
  }
  while (!title.empty() && (title.back() == ' ' || title.back() == '.')) title.pop_back();
  char tail[16];
  std::snprintf(tail, sizeof tail, "-%02X-%02X%02X", head[0x14D], head[0x14E], head[0x14F]);
  out.system = System::GB;
  out.internal_name = title;
  out.id = std::string(color ? "CGB-" : "DMG-") + (title.empty() ? std::string("UNTITLED") : title) + tail;
  out.region = head[0x14A] == 0 ? "Japan" : "";
  return true;
}

// Fills `out` from a game's first bytes. Returns false when they are not an N64, GBA or Game Boy header.
bool describe(const savekit::Bytes& head, const fs::path& path, const std::string& entry, Game& out) {
  out.path = path;
  out.entry = entry;
  out.title = display_title(entry.empty() ? path : fs::path(std::u8string(entry.begin(), entry.end())).filename());
  if (describe_gba(head, out) || describe_gb(head, out)) {
    if (out.internal_name.empty()) out.internal_name = out.title;
    return true;
  }
  auto h = savekit::parse_rom_header(savekit::Bytes(head.begin(), head.begin() + std::min<std::size_t>(head.size(), 0x40)));
  if (!h) return false;
  out.system = System::N64;
  out.internal_name = h->internal_name.empty() ? out.title : h->internal_name;
  out.id = h->game_id();
  out.region = region_name(h->country);
  return true;
}

}  // namespace

bool is_game_file(const fs::path& file) {
  return has_game_extension(file) || psp::is_psp_extension(file) || archive::is_archive(file);
}

std::vector<Game> read_games(const fs::path& file, std::string* error) {
  std::vector<Game> games;
  if (error) error->clear();
  if (archive::is_archive(file)) {
    // Every file in the archive is checked by its header, whatever its name: archives often hold
    // a game under a .bin or .rom name, next to a readme.
    const std::string err = archive::scan(file, kHeadSize, [&](const archive::Entry& e, const std::vector<uint8_t>& head) {
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
  // PSP games: a disc image, read in place (never packed: an archive would have to be unpacked
  // whole, up to 1.8 GB, every time the game starts).
  if (psp::is_psp_extension(file)) {
    psp::DiscInfo disc;
    if (psp::read_disc(file, disc)) {
      Game g;
      g.system = System::PSP;
      g.path = file;
      // The game's own name, as the PSP's menu shows it, when the app's font can show it
      // (Latin letters); otherwise the file's name, as for the other consoles.
      const bool latin = !disc.title.empty() && std::all_of(disc.title.begin(), disc.title.end(), [](unsigned char c) {
        return c >= 0x20 && c < 0x7F;
      });
      g.title = latin ? disc.title : display_title(file);
      // Without an ID (homebrew), the name, made safe as a folder name: its saves and states go in
      // a folder by that name.
      std::string folder = disc.disc_id.empty() ? g.title : disc.disc_id;
      for (char& c : folder) {
        if (static_cast<unsigned char>(c) < 0x20 || std::string("<>:\"/\\|?*").find(c) != std::string::npos) c = '_';
      }
      while (!folder.empty() && (folder.back() == '.' || folder.back() == ' ')) folder.pop_back();
      g.internal_name = folder.empty() ? "PSP game" : folder;
      g.id = "PSP-" + (disc.disc_id.empty() ? g.title : disc.disc_id);
      g.region = disc.region;
      games.push_back(std::move(g));
    }
    return games;
  }
  std::ifstream in(file, std::ios::binary);
  savekit::Bytes head(kHeadSize);
  in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
  head.resize(static_cast<std::size_t>(in.gcount()));
  Game g;
  if (head.size() >= 0x40 && describe(head, file, {}, g)) games.push_back(std::move(g));
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
