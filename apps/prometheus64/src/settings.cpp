#include "settings.hpp"

#include <fstream>

namespace p64 {
namespace {

std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) return {};
  return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}

}  // namespace

void Settings::load(const std::filesystem::path& file) {
  file_ = file;
  values_.clear();
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    values_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
  }
}

void Settings::save() const {
  if (file_.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(file_.parent_path(), ec);
  const auto tmp = std::filesystem::path(file_).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << "# Project Prometheus settings\n";
    for (const auto& [k, v] : values_) out << k << " = " << v << "\n";
  }
  std::filesystem::rename(tmp, file_, ec);
  if (ec) {
    std::filesystem::remove(file_, ec);
    std::filesystem::rename(tmp, file_, ec);
  }
}

std::string Settings::get(const std::string& key, const std::string& fallback) const {
  auto it = values_.find(key);
  return it == values_.end() ? fallback : it->second;
}

int Settings::get_int(const std::string& key, int fallback) const {
  try {
    return std::stoi(get(key, std::to_string(fallback)));
  } catch (...) {
    return fallback;
  }
}

void Settings::set(const std::string& key, const std::string& value) { values_[key] = value; }

std::string Settings::game(const std::string& id, const std::string& key, const std::string& fallback) const {
  auto it = values_.find("game." + id + "." + key);
  return it != values_.end() ? it->second : get(key, fallback);
}

int Settings::game_int(const std::string& id, const std::string& key, int fallback) const {
  try {
    return std::stoi(game(id, key, std::to_string(fallback)));
  } catch (...) {
    return fallback;
  }
}

void Settings::set_game(const std::string& id, const std::string& key, const std::string& value) {
  values_["game." + id + "." + key] = value;
}

}  // namespace p64
