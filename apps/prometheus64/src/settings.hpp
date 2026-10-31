// Settings, saved as plain "key = value" lines. Per-game values use "game.<ID>.<key>" and fall
// back to the global value, so a game remembers its own picture and fill mode.
#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace p64 {

class Settings {
 public:
  void load(const std::filesystem::path& file);
  void save() const;

  std::string get(const std::string& key, const std::string& fallback = "") const;
  int get_int(const std::string& key, int fallback) const;
  void set(const std::string& key, const std::string& value);
  void set_int(const std::string& key, int value) { set(key, std::to_string(value)); }

  // Per-game value, else the global one, else `fallback`.
  std::string game(const std::string& id, const std::string& key, const std::string& fallback) const;
  int game_int(const std::string& id, const std::string& key, int fallback) const;
  void set_game(const std::string& id, const std::string& key, const std::string& value);
  void set_game_int(const std::string& id, const std::string& key, int value) {
    set_game(id, key, std::to_string(value));
  }
  // Forgets `key` for every game, so all of them use the global value or the fallback again.
  void clear_game_key(const std::string& key);

 private:
  std::filesystem::path file_;
  std::map<std::string, std::string> values_;
};

}  // namespace p64
