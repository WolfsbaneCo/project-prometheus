// Interface constants and helpers shared by the app's screens (app.cpp, app_online.cpp).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "ui.hpp"

namespace p64 {

// Interface colours.
inline const Color kBg0 = rgb(0x17100F), kBg1 = rgb(0x0A0A0B), kPanel = rgb(0x18181B), kTile = rgb(0x1D1D21),
            kRed = rgb(0xE8262E), kText = rgb(0xF4F1EE), kDim = rgb(0xA59F9B), kFaint = rgb(0x6E6966);
inline const Color kPlayerColors[4] = {rgb(0xE8262E), rgb(0x3D7BF0), rgb(0x2FB45A), rgb(0xF2C12E)};

enum UiAction { kUp, kDown, kLeft, kRight, kAccept, kBack, kMenu, kPrevTab, kNextTab };
enum DialogKind { kFolderChosen = 1, kSaveChosen = 2 };
enum MenuId {
  kResume, kSaveState, kLoadState, kSlot, kPicture, kTextures, kFill, kResolution, kOverscan, kVolume, kFullscreenItem,
  kPlayers, kControlsItem, kReset, kQuit
};
// Clickable areas (App::Hit::kind).
enum HitKind {
  kHitPanel, kHitTile, kHitAddGames, kHitHint, kHitMenuRow, kHitMenuDec, kHitMenuInc, kHitMenuButton, kHitTab,
  kHitEntry, kHitSeatingDone, kHitSeatRemove, kHitSeatingReset, kHitOnline, kHitField
};
// Footer shortcuts on the library screen (kHitHint arg).
enum HintId { kHintPlay, kHintFolder, kHintImport, kHintOnline, kHintControls, kHintPlayers, kHintFullscreen, kHintHelp };
inline constexpr int kEntryReset = -1, kEntryDone = -2;
inline constexpr int kResSuper = 9;  // "resolution" setting: 0 auto, 1-8 fixed factor, 9 supersampled
inline constexpr uint64_t kMouseIdleNs = 2'500'000'000;  // in a game, the cursor hides after this

inline std::string utf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

inline std::filesystem::path from_utf8(const std::string& s) {
  return std::filesystem::path(std::u8string(s.begin(), s.end()));
}

// Breaks text into lines that fit `width`; the last line ends in "..." when there are too many.
inline std::vector<std::string> wrap(const Ui& ui, const std::string& text, float size, float width, bool bold, int max_lines) {
  std::vector<std::string> lines;
  std::string line, word;
  auto push_word = [&]() {
    if (word.empty()) return;
    const std::string trial = line.empty() ? word : line + " " + word;
    if (ui.measure(trial, size, bold) <= width || line.empty()) {
      line = trial;
    } else {
      lines.push_back(line);
      line = word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') {
      push_word();
    } else {
      word += c;
    }
  }
  push_word();
  if (!line.empty()) lines.push_back(line);
  if (static_cast<int>(lines.size()) > max_lines) {
    lines.resize(max_lines);
    lines.back() = ui.fit(lines.back() + " ...", size, width, bold);
  }
  for (auto& l : lines) l = ui.fit(l, size, width, bold);
  return lines;
}

}  // namespace p64
