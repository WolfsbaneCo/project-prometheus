#include "app.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "archive.hpp"
#include "savekit/formats.hpp"
#include "savekit/library.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr std::size_t kSrmSize = 0x48800;  // Mupen64Plus-Next save memory layout
constexpr const char* kOpt = "mupen64plus-";

// Interface colours.
const Color kBg0 = rgb(0x17100F), kBg1 = rgb(0x0A0A0B), kPanel = rgb(0x18181B), kTile = rgb(0x1D1D21),
            kRed = rgb(0xE8262E), kText = rgb(0xF4F1EE), kDim = rgb(0xA59F9B), kFaint = rgb(0x6E6966);
const Color kPlayerColors[4] = {rgb(0xE8262E), rgb(0x3D7BF0), rgb(0x2FB45A), rgb(0xF2C12E)};

enum UiAction { kUp, kDown, kLeft, kRight, kAccept, kBack, kMenu };
enum DialogKind { kFolderChosen = 1, kSaveChosen = 2 };
enum MenuId {
  kResume, kSaveState, kLoadState, kSlot, kPicture, kFill, kResolution, kOverscan, kVolume, kFullscreenItem,
  kPlayers, kReset, kQuit
};

struct PresetDef {
  const char* name;
  const char* blurb;
  const char* rdp;  // renderer: GLideN64 (enhanced) or Angrylion (pixel-exact software)
  const char* rsp;
  int factor;  // 0 = match the display
  const char* bilinear;
  bool dither;
  int fxaa;
  float sharpen, scanlines;
  bool smooth;
};
const PresetDef kPresets[] = {
    {"Clean", "High resolution, no dither grain, smooth edges", "gliden64", "hle", 0, "3point", false, 1, 0.0f, 0.0f, true},
    {"Sharp", "Clean, with extra crispness", "gliden64", "hle", 0, "3point", false, 1, 0.45f, 0.0f, true},
    {"Retro", "CRT-style scanlines", "gliden64", "hle", 2, "3point", false, 0, 0.0f, 0.65f, true},
    {"Native", "Pixel-exact original picture (uses more CPU)", "angrylion", "parallel", 1, "3point", true, 0, 0.0f, 0.0f, false},
};

std::string utf8(const fs::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

fs::path from_utf8(const std::string& s) {
  return fs::path(std::u8string(s.begin(), s.end()));
}

std::vector<std::string> wrap(const Ui& ui, const std::string& text, float size, float width, bool bold, int max_lines) {
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

// A Controller Pak holds a game's data when its note table (pages 3-4) names a game.
bool pak_has_notes(const savekit::Bytes& pak) {
  for (std::size_t at = 0x300; at + 32 <= 0x500 && at + 32 <= pak.size(); at += 32) {
    if (pak[at] || pak[at + 1] || pak[at + 2] || pak[at + 3]) return true;
  }
  return false;
}

// Unpacks the game when it is in an archive and hands it to the core.
std::string load_into(Core& core, const Game& game) {
  std::vector<uint8_t> data;
  if (std::string err = load_game_data(game, data); !err.empty()) return err;
  const fs::path name = game.entry.empty() ? game.path : game.path.parent_path() / from_utf8(game.entry).filename();
  return core.load_game(name, std::move(data));
}

void SDLCALL dialog_done(void* userdata, const char* const* files, int) {
  auto* info = static_cast<std::pair<uint32_t, int>*>(userdata);
  if (files && files[0]) {
    SDL_Event e{};
    e.type = info->first;
    e.user.code = info->second;
    e.user.data1 = SDL_strdup(files[0]);
    SDL_PushEvent(&e);
  }
  delete info;
}

}  // namespace

const char* preset_name(Preset p) { return kPresets[static_cast<int>(p)].name; }

App::App(Options options) : opt_(std::move(options)) {}

int App::run() {
  std::string error;
  if (!init(error)) {
    SDL_Log("Prometheus 64 could not start: %s", error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Prometheus 64", error.c_str(), window_);
    shutdown();
    return 1;
  }
  loop();
  shutdown();
  return 0;
}

bool App::init(std::string& error) {
  SDL_SetAppMetadata("Prometheus 64", "0.1.0", "org.projectprometheus.prometheus64");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    error = SDL_GetError();
    return false;
  }
  user_event_ = SDL_RegisterEvents(1);

  if (char* pref = SDL_GetPrefPath("Project Prometheus", "Prometheus 64")) {
    pref_dir_ = from_utf8(pref);
    SDL_free(pref);
  }
  assets_dir_ = from_utf8(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "assets";
  settings_.load(pref_dir_ / "settings.ini");
  const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  const fs::path default_library = (docs ? from_utf8(docs) : pref_dir_) / "N64 Saves";
  library_dir_ = from_utf8(settings_.get("library", utf8(default_library)));
  slot_ = std::clamp(settings_.get_int("state_slot", 1), 1, 10);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  window_ = SDL_CreateWindow("Prometheus 64", opt_.width, opt_.height,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (!window_) {
    error = std::string("could not open a window: ") + SDL_GetError();
    return false;
  }
  SDL_SetWindowMinimumSize(window_, 640, 400);
  gl_ = SDL_GL_CreateContext(window_);
  if (!gl_) {
    error = std::string("this PC's graphics driver does not offer OpenGL 3.3: ") + SDL_GetError();
    return false;
  }
  SDL_GL_MakeCurrent(window_, gl_);
  if (const char* missing = gl::load()) {
    error = std::string("graphics driver is missing ") + missing;
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  if (!video_.init(error) || !ui_.init(assets_dir_, error)) return false;
  if (SDL_Surface* icon = SDL_LoadPNG(utf8(assets_dir_ / "icon.png").c_str())) {
    SDL_SetWindowIcon(window_, icon);
    SDL_DestroySurface(icon);
  }

  // The emulator core.
  core_.system_dir = pref_dir_ / "system";
  core_.save_dir = pref_dir_ / "core";
  std::error_code ec;
  fs::create_directories(core_.system_dir, ec);
  fs::create_directories(core_.save_dir, ec);
  fs::path core_path = opt_.core;
  if (core_path.empty()) {
    const fs::path base = from_utf8(SDL_GetBasePath() ? SDL_GetBasePath() : "");
#if defined(_WIN32)
    core_path = base / "cores" / "mupen64plus_next_libretro.dll";
#elif defined(__APPLE__)
    core_path = base / "cores" / "mupen64plus_next_libretro.dylib";
#else
    core_path = base / "cores" / "mupen64plus_next_libretro.so";
#endif
  }
  core_.hooks.video = [this](const void* data, unsigned w, unsigned h, std::size_t pitch) {
    static const bool debug = SDL_getenv("P64_DEBUG") != nullptr;
    static int logged = 0;
    if (debug && (logged < 4 || frame_count_ % 30 == 0) && logged < 20) {
      ++logged;
      uint32_t nonzero = 0;
      if (data && data != RETRO_HW_FRAME_BUFFER_VALID) {
        for (unsigned y = 0; y < h; ++y) {
          const auto* row = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(data) + y * pitch);
          for (unsigned x = 0; x < w; ++x) nonzero += (row[x] & 0xFFFFFF) != 0;
        }
      }
      SDL_Log("video: %s %ux%u pitch %llu format %d nonzero %u",
              !data ? "dupe" : data == RETRO_HW_FRAME_BUFFER_VALID ? "hw" : "sw", w, h,
              static_cast<unsigned long long>(pitch), static_cast<int>(core_.pixel_format()), nonzero);
    }
    if (!data) return;  // duplicate frame: keep the last one
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
      video_.hardware_frame(w, h);
    } else {
      video_.software_frame(data, w, h, pitch, core_.pixel_format());
    }
  };
  core_.hooks.audio = [this](const int16_t* d, std::size_t n) {
    static const bool debug = SDL_getenv("P64_DEBUG") != nullptr;
    if (debug) {
      static uint64_t frames = 0, loud = 0, peak = 0;
      for (std::size_t i = 0; i < n * 2; ++i) {
        const uint64_t a = static_cast<uint64_t>(std::abs(static_cast<int>(d[i])));
        peak = std::max(peak, a);
        loud += a > 512;
      }
      if (frames == 0 || (frames + n) / 44100 != frames / 44100) {
        SDL_Log("audio: %llu frames so far, %llu loud samples, peak %llu, queued %.3fs",
                static_cast<unsigned long long>(frames + n), static_cast<unsigned long long>(loud),
                static_cast<unsigned long long>(peak), audio_.queued_seconds());
      }
      frames += n;
    }
    audio_.push(d, n);
  };
  core_.hooks.input_poll = [] {};
  core_.hooks.input_state = [this](unsigned port, unsigned dev, unsigned idx, unsigned id) {
    return input_.retro_state(port, dev, idx, id);
  };
  core_.hooks.rumble = [this](unsigned port, retro_rumble_effect effect, uint16_t strength) {
    return input_.rumble(port, effect == RETRO_RUMBLE_STRONG, strength);
  };
  core_.hooks.message = [this](const std::string& m) { toast(m); };
  core_.hooks.framebuffer = [this] { return video_.target_framebuffer(); };
  core_.hooks.proc_address = [](const char* name) {
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(name));
  };
  core_.hooks.geometry_changed = [this] {
    video_.prepare_target(core_.av().geometry.max_width, core_.av().geometry.max_height);
  };
  core_path_ = core_path;
  error = core_.open(core_path);
  if (!error.empty()) {
    error += "\n\nExpected the emulator core at:\n" + utf8(core_path) + "\n\nReinstalling Prometheus 64 fixes this.";
    return false;
  }

  input_.init();
  rescan();
  set_fullscreen(opt_.fullscreen || settings_.get_int("fullscreen", 0) == 1);
  if (!opt_.rom.empty()) {
    std::string why;
    auto found = read_games(opt_.rom, &why);
    if (found.empty()) {
      error = why.empty() ? "No N64 game found in " + utf8(opt_.rom) : "Could not read " + utf8(opt_.rom) + ": " + why;
      return false;
    }
    if (!start_game(found.front())) {
      error = "could not start " + utf8(opt_.rom);
      return false;
    }
  }
  return true;
}

void App::shutdown() {
  if (game_) stop_game();
  core_.close();
  settings_.save();
  if (gl_) {
    ui_.shutdown();
    video_.shutdown();
  }
  audio_.close();
  if (gl_) SDL_GL_DestroyContext(gl_);
  if (window_) SDL_DestroyWindow(window_);
  gl_ = nullptr;
  window_ = nullptr;
  SDL_Quit();
}

void App::rescan() {
  games_ = scan_games(from_utf8(settings_.get("games_folder")));
  selected_ = std::clamp(selected_, 0, std::max(0, static_cast<int>(games_.size()) - 1));
}

void App::toast(const std::string& text, double seconds) {
  toasts_.push_back({text, SDL_GetTicksNS() + static_cast<uint64_t>(seconds * 1e9)});
  while (toasts_.size() > 4) toasts_.pop_front();
}

void App::set_fullscreen(bool on) {
  fullscreen_ = on;
  SDL_SetWindowFullscreen(window_, on);  // borderless: uses the desktop's own resolution and refresh
  settings_.set_int("fullscreen", on ? 1 : 0);
}

// ---------------------------------------------------------------------------------------------
// Games

int App::resolution_factor() const {
  const std::string id = game_ ? game_->id : "";
  const Preset preset = static_cast<Preset>(std::clamp(settings_.game_int(id, "preset", 0), 0, 3));
  const int fixed = kPresets[static_cast<int>(preset)].factor;
  if (fixed > 0) return fixed;
  const int chosen = settings_.game_int(id, "resolution", 0);
  if (chosen > 0) return std::clamp(chosen, 1, 8);
  // Match the screen: 1080p -> 5x (rendered at 1200 lines, then downscaled), 1440p -> 6x, 4K -> 8x.
  int h = 1080;
  if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_))) h = m->h;
  return std::clamp((h + 239) / 240, 1, 8);
}

void App::apply_core_options(const Game& game) {
  const PresetDef& p = kPresets[std::clamp(settings_.game_int(game.id, "preset", 0), 0, 3)];
  const bool widescreen = settings_.game(game.id, "fill", "") == std::to_string(static_cast<int>(FillMode::Widescreen));
  auto set = [&](const char* key, const std::string& value) { core_.set_option(std::string(kOpt) + key, value); };
  set("rdp-plugin", settings_.get("renderer", p.rdp));
  set("rsp-plugin", settings_.get("renderer", p.rdp) == std::string("gliden64") ? "hle" : p.rsp);
  set("cpucore", "dynamic_recompiler");
  set("EnableNativeResFactor", std::to_string(resolution_factor()));
  set("BilinearMode", p.bilinear);
  set("DitheringPattern", p.dither ? "True" : "False");
  set("DitheringQuantization", p.dither ? "True" : "False");
  set("RDRAMImageDitheringMode", p.dither ? "BlueNoise" : "False");
  set("FXAA", std::to_string(p.fxaa));
  set("EnableOverscan", "Disabled");
  set("aspect", widescreen ? "16:9 adjusted" : "4:3");
  set("alt-map", "True");
  set("astick-deadzone", "15");
  set("ForceDisableExtraMem", "False");
  // Rumble for players whose controller can rumble, a Controller Pak for everyone else.
  for (int i = 0; i < Input::kPlayers; ++i) {
    set(("pak" + std::to_string(i + 1)).c_str(), input_.has_rumble(i) ? "rumble" : "memory");
  }
  // Texture packs: <system>/Mupen64plus/hires_texture/<internal name>/
  std::error_code ec;
  const bool pack = fs::is_directory(core_.system_dir / "Mupen64plus" / "hires_texture" / game.internal_name, ec);
  set("txHiresEnable", pack ? "True" : "False");
  set("txHiresFullAlphaChannel", pack ? "True" : "False");
}

bool App::start_game(const Game& game) {
  if (game_) stop_game();
  apply_core_options(game);
  const std::string err = load_into(core_, game);
  if (!err.empty()) {
    toast("Could not start " + game.title + ": " + err, 5);
    return false;
  }
  game_ = game;
  if (core_.hardware_rendering()) {
    video_.prepare_target(core_.av().geometry.max_width, core_.av().geometry.max_height);
    core_.context_reset();
  }
  video_.clear_frame();
  audio_.open(core_.av().timing.sample_rate > 0 ? core_.av().timing.sample_rate : 44100.0);
  audio_.set_volume(settings_.get_int("volume", 80) / 100.0f);
  load_game_save();
  screen_ = Screen::Playing;
  overlay_ = Overlay::None;
  acc_ns_ = 0;
  last_ns_ = SDL_GetTicksNS();
  settings_.set("last_game", utf8(game.path));
  toast(game.title + "  -  press Esc (or Back on a controller) for the menu", 5);
  if (input_.keyboard_shared()) toast("Two players share the keyboard: gamepads work better", 5);
  return true;
}

void App::stop_game() {
  if (!game_) return;
  sync_game_save(true);
  core_.context_destroy();
  core_.close();
  if (const std::string err = core_.open(core_path_); !err.empty()) toast(err, 8);
  audio_.close();
  video_.clear_frame();
  game_.reset();
  screen_ = Screen::Home;
  overlay_ = Overlay::None;
  settings_.save();
}

void App::restart_game_keep_progress() {
  if (!game_) return;
  // Graphics options are read when a game starts, so restart it and put everything back exactly
  // where it was with a save state taken in memory.
  const Game game = *game_;
  const std::vector<uint8_t> state = core_.save_state();
  std::vector<uint8_t> ram;
  if (uint8_t* r = core_.save_ram()) ram.assign(r, r + core_.save_ram_size());
  sync_game_save(true);
  core_.context_destroy();
  // This core cannot start a second game in one session, so reload the core library itself
  // (as RetroArch does when it restarts a core).
  core_.close();
  if (!core_.open(core_path_).empty()) {
    toast("Could not restart the emulator core");
    game_.reset();
    screen_ = Screen::Home;
    return;
  }
  apply_core_options(game);
  if (!load_into(core_, game).empty()) {
    toast("Could not restart the game");
    game_.reset();
    screen_ = Screen::Home;
    return;
  }
  if (core_.hardware_rendering()) {
    video_.prepare_target(core_.av().geometry.max_width, core_.av().geometry.max_height);
    core_.context_reset();
  }
  if (uint8_t* r = core_.save_ram(); r && ram.size() == core_.save_ram_size()) std::memcpy(r, ram.data(), ram.size());
  core_.run();  // let the core settle before restoring
  if (!core_.load_state(state)) toast("Picture changed. The game restarted from your last save");
}

Look App::current_look() const {
  Look l;
  const std::string id = game_ ? game_->id : "";
  const PresetDef& p = kPresets[std::clamp(settings_.game_int(id, "preset", 0), 0, 3)];
  l.fill = static_cast<FillMode>(std::clamp(settings_.game_int(id, "fill", 0), 0, static_cast<int>(FillMode::Count) - 1));
  l.overscan = std::clamp(settings_.game_int(id, "overscan", 0), 0, 8) / 100.0f;
  l.sharpen = p.sharpen;
  l.scanlines = p.scanlines;
  l.smooth = p.smooth;
  const float a = core_.av().geometry.aspect_ratio;
  l.aspect = a > 0 ? a : 4.0f / 3.0f;
  return l;
}

// ---------------------------------------------------------------------------------------------
// Saves

void App::load_game_save() {
  last_saved_ram_.clear();
  pending_ram_.clear();
  uint8_t* ram = core_.save_ram();
  if (!ram || core_.save_ram_size() != kSrmSize) return;
  try {
    if (auto saved = savekit::load_current(library_dir_, game_->internal_name)) {
      // Start from the core's freshly formatted memory and lay the saved memories over it, so
      // anything the save does not hold (an empty Controller Pak) stays properly formatted.
      savekit::Bytes fresh(ram, ram + kSrmSize);
      savekit::SaveSet merged = savekit::decode(savekit::Format::RetroArchMupen64PlusNext, {{".srm", fresh}}).save;
      for (const auto& [memory, bytes] : saved->memories) merged.memories[memory] = bytes;
      const savekit::Shape full{{".srm", kSrmSize}};
      const auto srm = savekit::encode(savekit::Format::RetroArchMupen64PlusNext, merged, &full).at(".srm");
      std::memcpy(ram, srm.data(), kSrmSize);
      toast("Loaded your saved progress");
    }
  } catch (const std::exception& e) {
    toast(std::string("Could not load the save: ") + e.what(), 6);
  }
  last_saved_ram_.assign(ram, ram + kSrmSize);
}

void App::sync_game_save(bool force) {
  uint8_t* ram = core_.save_ram();
  if (!game_ || !ram || core_.save_ram_size() != kSrmSize) return;
  std::vector<uint8_t> now(ram, ram + kSrmSize);
  if (now == last_saved_ram_) {
    pending_ram_.clear();
    return;
  }
  // Wait until the game has stopped writing (unchanged for about a second) before copying.
  if (!force && now != pending_ram_) {
    pending_ram_ = std::move(now);
    pending_since_frame_ = frame_count_;
    return;
  }
  if (!force && frame_count_ - pending_since_frame_ < 60) return;
  try {
    savekit::StoreOptions o;
    o.library = library_dir_;
    o.game = game_->internal_name;
    o.save = savekit::decode(savekit::Format::RetroArchMupen64PlusNext, {{".srm", savekit::Bytes(now)}}).save;
    // The core formats every Controller Pak; only keep the ones that hold notes.
    for (auto m : {savekit::Memory::ControllerPak1, savekit::Memory::ControllerPak2, savekit::Memory::ControllerPak3,
                   savekit::Memory::ControllerPak4}) {
      auto it = o.save.memories.find(m);
      if (it != o.save.memories.end() && !pak_has_notes(it->second)) o.save.memories.erase(it);
    }
    if (o.save.memories.empty()) {
      last_saved_ram_ = now;
      return;
    }
    if (savekit::store_current(o).changed) toast("Progress saved", 2);
    last_saved_ram_ = now;
    pending_ram_.clear();
  } catch (const std::exception& e) {
    toast(std::string("Could not save progress: ") + e.what(), 6);
  }
}

fs::path App::state_path(int slot) const {
  return library_dir_ / game_->internal_name / "states" / ("slot" + std::to_string(slot) + ".state");
}

void App::save_state() {
  if (!game_) return;
  const auto data = core_.save_state();
  if (data.empty()) {
    toast("This game cannot make a save state right now");
    return;
  }
  const fs::path file = state_path(slot_);
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path tmp = fs::path(file).concat(".tmp");
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  }
  fs::rename(tmp, file, ec);
  if (ec) {
    fs::remove(file, ec);
    fs::rename(tmp, file, ec);
  }
  // Save states only load in the same emulator core version, so record which one made it.
  std::ofstream(fs::path(file).replace_extension(".json"))
      << "{\n  \"core\": \"" << (core_.info().library_name ? core_.info().library_name : "") << "\",\n  \"version\": \""
      << (core_.info().library_version ? core_.info().library_version : "") << "\"\n}\n";
  toast("Saved state " + std::to_string(slot_));
}

void App::load_state() {
  if (!game_) return;
  std::ifstream in(state_path(slot_), std::ios::binary);
  if (!in) {
    toast("State " + std::to_string(slot_) + " is empty");
    return;
  }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (core_.load_state(data)) {
    toast("Loaded state " + std::to_string(slot_));
  } else {
    toast("State " + std::to_string(slot_) + " was made by a different emulator version and cannot be loaded");
  }
}

void App::choose_games_folder() {
  auto* info = new std::pair<uint32_t, int>(user_event_, kFolderChosen);
  const std::string current = settings_.get("games_folder");
  SDL_ShowOpenFolderDialog(dialog_done, info, window_, current.empty() ? nullptr : current.c_str(), false);
}

void App::choose_save_to_import() {
  if (games_.empty()) return;
  static const SDL_DialogFileFilter filters[] = {{"N64 save files", "srm;sra;eep;fla;mpk"}};
  auto* info = new std::pair<uint32_t, int>(user_event_, kSaveChosen);
  SDL_ShowOpenFileDialog(dialog_done, info, window_, filters, 1, nullptr, false);
}

void App::import_save(const fs::path& file) {
  if (games_.empty()) return;
  const Game& g = games_[selected_];
  try {
    std::string ext = file.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::ifstream in(file, std::ios::binary);
    savekit::Bytes data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Recognise the file by its type and size; other emulators' files share these layouts.
    savekit::Decoded d;
    if (ext == ".srm") {
      d = savekit::decode(savekit::Format::RetroArchMupen64PlusNext, {{".srm", data}});
    } else if (ext == ".mpk" && data.size() == 4 * savekit::kControllerPakSize) {
      d = savekit::decode(savekit::Format::Mupen64Plus, {{".mpk", data}});
    } else if (ext == ".mpk") {
      d = savekit::decode(savekit::Format::Project64, {{"_Cont_1.mpk", data}});
    } else {
      d = savekit::decode(savekit::Format::Project64, {{ext, data}});
    }
    if (d.save.memories.empty()) {
      toast("That save file is empty");
      return;
    }
    savekit::StoreOptions o;
    o.library = library_dir_;
    o.game = g.internal_name;
    o.save = d.save;
    o.source = "imported save file";
    savekit::store_current(o);
    toast("Save imported for " + g.title + ". Your previous save is kept in history", 5);
  } catch (const std::exception& e) {
    toast(std::string("Could not import that save: ") + e.what(), 6);
  }
}

void App::take_screenshot(const fs::path& file) {
  std::vector<uint8_t> rgb(static_cast<std::size_t>(out_w_) * out_h_ * 3);
  if (!video_.read_pixels(out_w_, out_h_, rgb.data())) {
    SDL_Log("screenshot: could not read the picture back");
    return;
  }
  SDL_Surface* s = SDL_CreateSurface(out_w_, out_h_, SDL_PIXELFORMAT_RGB24);
  if (!s) return;
  for (int y = 0; y < out_h_; ++y) {
    std::memcpy(static_cast<uint8_t*>(s->pixels) + static_cast<std::size_t>(y) * s->pitch,
                rgb.data() + static_cast<std::size_t>(out_h_ - 1 - y) * out_w_ * 3, static_cast<std::size_t>(out_w_) * 3);
  }
  std::error_code ec;
  if (file.has_parent_path()) fs::create_directories(file.parent_path(), ec);
  if (!SDL_SavePNG(s, utf8(file).c_str())) SDL_Log("screenshot: %s", SDL_GetError());
  SDL_DestroySurface(s);
}

// ---------------------------------------------------------------------------------------------
// Main loop

void App::run_frames() {
  const uint64_t now = SDL_GetTicksNS();
  acc_ns_ += now - last_ns_;
  last_ns_ = now;
  const double fps = core_.av().timing.fps > 1 ? core_.av().timing.fps : 60.0;
  const uint64_t frame_ns = static_cast<uint64_t>(1e9 / fps);
  if (acc_ns_ > 4 * frame_ns) acc_ns_ = frame_ns;  // after a pause or a hitch, don't race to catch up
  if (screen_ != Screen::Playing || overlay_ != Overlay::None) {
    acc_ns_ = 0;
    return;
  }
  for (int runs = 0; acc_ns_ >= frame_ns && runs < 3; ++runs) {
    core_.run();
    acc_ns_ -= frame_ns;
    ++frame_count_;
    if (frame_count_ % 30 == 0) sync_game_save(false);
  }
}

void App::loop() {
  last_ns_ = SDL_GetTicksNS();
  uint64_t frames_drawn = 0, swap_ns_total = 0;
  std::size_t next_key = 0;
  while (running_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) handle(e);
    if (!running_) break;

    // Test mode: scripted key presses ("frame:scancode-name").
    while (next_key < opt_.keys.size()) {
      const std::string& k = opt_.keys[next_key];
      const auto colon = k.find(':');
      if (std::stoull(k.substr(0, colon)) > frames_drawn) break;
      SDL_Event key{};
      key.type = SDL_EVENT_KEY_DOWN;
      key.key.scancode = SDL_GetScancodeFromName(k.substr(colon + 1).c_str());
      key.key.down = true;
      handle(key);
      ++next_key;
    }

    // Test mode: held N64 buttons ("from-to:player:button").
    for (const auto& h : opt_.holds) {
      const auto dash = h.find('-'), c1 = h.find(':'), c2 = h.find(':', c1 + 1);
      const uint64_t from = std::stoull(h.substr(0, dash)), to = std::stoull(h.substr(dash + 1, c1 - dash - 1));
      const int player = std::stoi(h.substr(c1 + 1, c2 - c1 - 1)) - 1;
      if (frames_drawn == from) input_.test_press(player, h.substr(c2 + 1), true);
      if (frames_drawn == to) input_.test_press(player, h.substr(c2 + 1), false);
    }

    const uint64_t t0 = SDL_GetTicksNS();
    input_.update(static_cast<float>(std::min<uint64_t>(t0 - last_ns_, 100'000'000)) / 1e9f);
    for (auto& n : input_.take_notices()) toast(n, 4);
    if (game_) run_frames();
    audio_.set_paused(!game_ || overlay_ != Overlay::None);
    draw();
    ++frames_drawn;

    if (game_ && opt_.poke_frame > 0 && frames_drawn == static_cast<uint64_t>(opt_.poke_frame)) {
      // Test mode: the "game" writes to its SRAM (offset 0x20800 in the core's save layout).
      if (uint8_t* ram = core_.save_ram(); ram && core_.save_ram_size() == kSrmSize) {
        std::memcpy(ram + 0x20800, "MORPEHTE", 8);  // word-swapped, so the canonical save reads "PROMETHE"
      }
    }
    if (!opt_.screenshot.empty() && static_cast<int>(frames_drawn) >= opt_.frames) {
      if (!opt_.dump_save.empty() && core_.save_ram()) {
        std::ofstream(opt_.dump_save, std::ios::binary)
            .write(reinterpret_cast<const char*>(core_.save_ram()), static_cast<std::streamsize>(core_.save_ram_size()));
      }
      take_screenshot(opt_.screenshot);
      running_ = false;
    }
    const uint64_t t1 = SDL_GetTicksNS();
    SDL_GL_SwapWindow(window_);
    swap_ns_total += SDL_GetTicksNS() - t1;
    // With working vsync the swap paces the loop; otherwise sleep until the next frame is due.
    if (frames_drawn == 30) vsync_working_ = swap_ns_total / 30 > 3'000'000;
    if (!vsync_working_) {
      const double fps = game_ && core_.av().timing.fps > 1 ? core_.av().timing.fps : 60.0;
      const uint64_t frame_ns = static_cast<uint64_t>(1e9 / fps);
      const uint64_t spent = SDL_GetTicksNS() - t0;
      if (spent < frame_ns) SDL_DelayPrecise(frame_ns - spent);
    }
  }
}

void App::handle(const SDL_Event& e) {
  if (e.type == SDL_EVENT_QUIT) {
    running_ = false;
    return;
  }
  if (e.type == user_event_) {
    std::string path = static_cast<char*>(e.user.data1);
    SDL_free(e.user.data1);
    if (e.user.code == kFolderChosen) {
      settings_.set("games_folder", path);
      settings_.save();
      rescan();
      toast(games_.empty() ? "No N64 games found in that folder" : "Found " + std::to_string(games_.size()) + " games");
    } else if (e.user.code == kSaveChosen) {
      import_save(from_utf8(path));
    }
    return;
  }
  if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
    const fs::path file = from_utf8(e.drop.data);
    std::string why;
    auto found = read_games(file, &why);
    if (!found.empty()) {
      if (settings_.get("games_folder").empty()) {
        settings_.set("games_folder", utf8(file.parent_path()));
        rescan();
      }
      start_game(found.front());
    } else if (!why.empty()) {
      toast("Could not open that archive: " + why, 5);
    } else {
      toast(archive::is_archive(file) ? "No N64 game in that archive" : "That file is not an N64 game");
    }
    return;
  }
  if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && game_ && overlay_ == Overlay::None && screen_ == Screen::Playing &&
      opt_.screenshot.empty()) {
    overlay_ = Overlay::Menu;  // pause when the window loses focus
    menu_index_ = 0;
  }

  // Seat players first, so a gamepad's first press joins instead of acting.
  if (input_.handle_event(e)) return;
  if (handle_hotkey(e)) return;

  if (overlay_ != Overlay::None) {
    handle_menu(e);
  } else if (screen_ == Screen::Home) {
    handle_home(e);
  } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
             (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK || e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)) {
    ui_action(kMenu);
  } else if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) {
    ui_action(kMenu);
  }
}

bool App::handle_hotkey(const SDL_Event& e) {
  if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
  const bool alt = (e.key.mod & SDL_KMOD_ALT) != 0, ctrl = (e.key.mod & SDL_KMOD_CTRL) != 0;
  switch (e.key.scancode) {
    case SDL_SCANCODE_F11:
      set_fullscreen(!fullscreen_);
      return true;
    case SDL_SCANCODE_RETURN:
      if (!alt) return false;
      set_fullscreen(!fullscreen_);
      return true;
    case SDL_SCANCODE_F1:
      overlay_ = overlay_ == Overlay::Help ? Overlay::None : Overlay::Help;
      return true;
    case SDL_SCANCODE_F10:
      if (overlay_ == Overlay::Seating) {
        input_.stop_seating();
        overlay_ = Overlay::None;
      } else {
        input_.start_seating();
        overlay_ = Overlay::Seating;
      }
      return true;
    case SDL_SCANCODE_F12:
      if (const char* pics = SDL_GetUserFolder(SDL_FOLDER_PICTURES)) {
        const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
        const fs::path f = from_utf8(pics) / "Prometheus 64" / ("screenshot-" + std::to_string(stamp) + ".png");
        take_screenshot(f);
        toast("Screenshot saved to " + utf8(f.parent_path()));
      }
      return true;
    default:
      break;
  }
  if (!game_ || overlay_ != Overlay::None) return false;
  switch (e.key.scancode) {
    case SDL_SCANCODE_F2:
      menu_change(kFill, 1);
      return true;
    case SDL_SCANCODE_F3:
      menu_change(kPicture, 1);
      return true;
    case SDL_SCANCODE_F5:
      save_state();
      return true;
    case SDL_SCANCODE_F6:
      menu_change(kSlot, 1);
      return true;
    case SDL_SCANCODE_F7:
      load_state();
      return true;
    case SDL_SCANCODE_R:
      if (!ctrl) return false;
      core_.reset();
      toast("Reset");
      return true;
    default:
      return false;
  }
}

void App::handle_home(const SDL_Event& e) {
  if (e.type == SDL_EVENT_KEY_DOWN) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: ui_action(kUp); break;
      case SDL_SCANCODE_DOWN: ui_action(kDown); break;
      case SDL_SCANCODE_LEFT: ui_action(kLeft); break;
      case SDL_SCANCODE_RIGHT: ui_action(kRight); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: ui_action(kAccept); break;
      case SDL_SCANCODE_O: choose_games_folder(); break;
      case SDL_SCANCODE_I: choose_save_to_import(); break;
      default: break;
    }
  } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_DPAD_UP: ui_action(kUp); break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN: ui_action(kDown); break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: ui_action(kLeft); break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: ui_action(kRight); break;
      case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: ui_action(kAccept); break;
      case SDL_GAMEPAD_BUTTON_NORTH: choose_games_folder(); break;
      default: break;
    }
  }
}

void App::handle_menu(const SDL_Event& e) {
  if (e.type == SDL_EVENT_KEY_DOWN) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: ui_action(kUp); break;
      case SDL_SCANCODE_DOWN: ui_action(kDown); break;
      case SDL_SCANCODE_LEFT: ui_action(kLeft); break;
      case SDL_SCANCODE_RIGHT: ui_action(kRight); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: ui_action(kAccept); break;
      case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: ui_action(kBack); break;
      default: break;
    }
  } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    switch (e.gbutton.button) {
      case SDL_GAMEPAD_BUTTON_DPAD_UP: ui_action(kUp); break;
      case SDL_GAMEPAD_BUTTON_DPAD_DOWN: ui_action(kDown); break;
      case SDL_GAMEPAD_BUTTON_DPAD_LEFT: ui_action(kLeft); break;
      case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: ui_action(kRight); break;
      case SDL_GAMEPAD_BUTTON_SOUTH: ui_action(kAccept); break;
      case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: case SDL_GAMEPAD_BUTTON_GUIDE: ui_action(kBack); break;
      default: break;
    }
  }
}

void App::ui_action(int action) {
  if (overlay_ == Overlay::Seating || overlay_ == Overlay::Help) {
    if (action == kBack || action == kAccept || action == kMenu) {
      input_.stop_seating();
      overlay_ = Overlay::None;
    }
    return;
  }
  if (overlay_ == Overlay::Menu) {
    const auto items = menu_items();
    const int n = static_cast<int>(items.size());
    switch (action) {
      case kUp: menu_index_ = (menu_index_ + n - 1) % n; break;
      case kDown: menu_index_ = (menu_index_ + 1) % n; break;
      case kLeft: menu_change(items[menu_index_].id, -1); break;
      case kRight: menu_change(items[menu_index_].id, 1); break;
      case kAccept: menu_change(items[menu_index_].id, 0); break;
      case kBack: case kMenu: overlay_ = Overlay::None; break;
      default: break;
    }
    return;
  }
  if (screen_ == Screen::Playing) {
    if (action == kMenu) {
      overlay_ = Overlay::Menu;
      menu_index_ = 0;
    }
    return;
  }
  // Home screen grid.
  const int count = static_cast<int>(games_.size());
  if (count == 0) {
    if (action == kAccept) choose_games_folder();
    return;
  }
  const float s = out_h_ / 1080.0f, tile_w = 330 * s, gap = 28 * s;
  const int cols = std::max(1, static_cast<int>((out_w_ - 2 * 72 * s + gap) / (tile_w + gap)));
  switch (action) {
    case kLeft: selected_ = std::max(0, selected_ - 1); break;
    case kRight: selected_ = std::min(count - 1, selected_ + 1); break;
    case kUp: if (selected_ - cols >= 0) selected_ -= cols; break;
    case kDown: selected_ = std::min(count - 1, selected_ + cols); break;
    case kAccept: start_game(games_[selected_]); break;
    default: break;
  }
}

std::vector<App::MenuItem> App::menu_items() const {
  const std::string id = game_ ? game_->id : "";
  const Look look = current_look();
  const int preset = std::clamp(settings_.game_int(id, "preset", 0), 0, 3);
  const int res_choice = settings_.game_int(id, "resolution", 0);
  std::string res = kPresets[preset].factor > 0 ? "Set by the picture style"
                    : res_choice > 0            ? std::to_string(res_choice) + "x"
                                                : "Auto (" + std::to_string(resolution_factor()) + "x)";
  return {
      {"Resume", "", kResume},
      {"Save state", "slot " + std::to_string(slot_), kSaveState},
      {"Load state", "slot " + std::to_string(slot_), kLoadState},
      {"State slot", std::to_string(slot_), kSlot},
      {"Picture", kPresets[preset].name, kPicture},
      {"Fill mode", fill_mode_name(look.fill), kFill},
      {"Resolution", res, kResolution},
      {"Overscan crop", std::to_string(static_cast<int>(std::lround(look.overscan * 100))) + "%", kOverscan},
      {"Volume", std::to_string(settings_.get_int("volume", 80)) + "%", kVolume},
      {"Fullscreen", fullscreen_ ? "On" : "Off", kFullscreenItem},
      {"Who's playing", std::to_string(input_.seated_count()) + (input_.seated_count() == 1 ? " player" : " players"), kPlayers},
      {"Reset game", "", kReset},
      {"Quit to library", "", kQuit},
  };
}

void App::menu_change(int id, int dir) {
  const std::string gid = game_ ? game_->id : "";
  const int step = dir == 0 ? 1 : dir;
  switch (id) {
    case kResume:
      if (dir == 0) overlay_ = Overlay::None;
      break;
    case kSaveState:
      if (dir == 0) {
        save_state();
        overlay_ = Overlay::None;
      }
      break;
    case kLoadState:
      if (dir == 0) {
        load_state();
        overlay_ = Overlay::None;
      }
      break;
    case kSlot:
      slot_ = (slot_ - 1 + step + 10) % 10 + 1;
      settings_.set_int("state_slot", slot_);
      if (overlay_ == Overlay::None) toast("State slot " + std::to_string(slot_));
      break;
    case kPicture: {
      const int p = (settings_.game_int(gid, "preset", 0) + step + 4) % 4;
      settings_.set_game_int(gid, "preset", p);
      toast(std::string("Picture: ") + kPresets[p].name + " - " + kPresets[p].blurb);
      restart_game_keep_progress();
      break;
    }
    case kFill: {
      const int count = static_cast<int>(FillMode::Count);
      const int before = settings_.game_int(gid, "fill", 0);
      const int f = (before + step + count) % count;
      settings_.set_game_int(gid, "fill", f);
      toast(std::string("Fill mode: ") + fill_mode_name(static_cast<FillMode>(f)) +
            (static_cast<FillMode>(f) == FillMode::Widescreen ? "  (works with many games, not all)" : ""));
      const int ws = static_cast<int>(FillMode::Widescreen);
      if ((before == ws) != (f == ws)) restart_game_keep_progress();
      break;
    }
    case kResolution: {
      const int r = (settings_.game_int(gid, "resolution", 0) + step + 9) % 9;  // 0 = auto, 1..8
      settings_.set_game_int(gid, "resolution", r);
      restart_game_keep_progress();
      break;
    }
    case kOverscan:
      settings_.set_game_int(gid, "overscan", std::clamp(settings_.game_int(gid, "overscan", 0) + step, 0, 8));
      break;
    case kVolume: {
      const int v = std::clamp(settings_.get_int("volume", 80) + 10 * step, 0, 100);
      settings_.set_int("volume", v);
      audio_.set_volume(v / 100.0f);
      break;
    }
    case kFullscreenItem:
      set_fullscreen(!fullscreen_);
      break;
    case kPlayers:
      if (dir == 0) {
        input_.start_seating();
        overlay_ = Overlay::Seating;
      }
      break;
    case kReset:
      if (dir == 0) {
        core_.reset();
        overlay_ = Overlay::None;
      }
      break;
    case kQuit:
      if (dir == 0) stop_game();
      break;
    default:
      break;
  }
  settings_.save();
}

// ---------------------------------------------------------------------------------------------
// Drawing

void App::draw() {
  SDL_GetWindowSizeInPixels(window_, &out_w_, &out_h_);
  video_.begin_present(out_w_, out_h_);
  if (screen_ == Screen::Playing && game_) {
    video_.draw_game(out_w_, out_h_, current_look());
  }
  ui_.begin(out_w_, out_h_);
  if (screen_ == Screen::Home) draw_home();
  if (overlay_ == Overlay::Menu) draw_menu();
  if (overlay_ == Overlay::Seating) draw_seating();
  if (overlay_ == Overlay::Help) draw_help();
  if (overlay_ == Overlay::None) draw_toasts();  // messages wait while a menu is open
  ui_.end();
  SDL_HideCursor();
  if (screen_ == Screen::Home || overlay_ != Overlay::None) SDL_ShowCursor();
}

void App::draw_players_strip(float x, float y, float s) {
  for (int i = 0; i < Input::kPlayers; ++i) {
    const Seat& seat = input_.seat(i);
    const float w = 190 * s, h = 40 * s, cx = x + i * (w + 10 * s);
    ui_.rect(cx, y, w, h, seat.empty() ? kTile.with_alpha(0.6f) : kTile, 12 * s);
    ui_.rect(cx + 12 * s, y + h / 2 - 6 * s, 12 * s, 12 * s, seat.empty() ? kFaint : kPlayerColors[i], 6 * s);
    const std::string label = "P" + std::to_string(i + 1) + "  " + (seat.empty() ? "Empty" : seat.name);
    ui_.text(cx + 32 * s, y + 9 * s, ui_.fit(label, 17 * s, w - 44 * s), 17 * s, seat.empty() ? kFaint : kText);
  }
}

void App::draw_home() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.gradient(0, 0, W, H, kBg0, kBg1);
  for (int i = 0; i < 8; ++i) {  // a soft ember glow in the corner, built from faint layers
    const float r = (360 + i * 90) * s;
    ui_.rect(-r * 0.6f, -r * 0.7f, r * 2, r * 1.5f, kRed.with_alpha(0.012f), r);
  }

  const float margin = 72 * s;
  ui_.image(ui_.logo(), margin, 54 * s, 72 * s, 72 * s);
  ui_.text(margin + 92 * s, 52 * s, "Prometheus 64", 44 * s, kText, true);
  ui_.text(margin + 94 * s, 106 * s, "Your 64 classics, kept alive", 21 * s, kDim);

  const float footer_h = 84 * s;
  ui_.rect(0, H - footer_h, W, footer_h, rgb(0x0C0C0D, 0.92f));
  const std::string hints = games_.empty() ? "O  Choose games folder      F10  Who's playing      F1  Help"
                                           : "Enter / A  Play      O / Y  Games folder      I  Import a save      F10  Who's playing      F1  Help";
  ui_.text(margin, H - footer_h + 30 * s, hints, 19 * s, kDim);
  draw_players_strip(W - margin - (4 * 190 + 3 * 10) * s, 70 * s, s);

  if (games_.empty()) {
    const float cy = H * 0.42f;
    ui_.image(ui_.logo(), W / 2 - 64 * s, cy - 150 * s, 128 * s, 128 * s, 0.9f);
    ui_.text(W / 2, cy, "Add your games", 40 * s, kText, true, Align::Center);
    ui_.text(W / 2, cy + 60 * s, "Press O (or Y on a controller) to choose the folder with your N64 game files.", 22 * s,
             kDim, false, Align::Center);
    ui_.text(W / 2, cy + 94 * s, "Zipped games (.zip, .7z, .rar) work too. You can also drop a game onto this window.", 22 * s, kDim, false, Align::Center);
    return;
  }

  const float tile_w = 330 * s, tile_h = 200 * s, gap = 28 * s, top = 180 * s;
  const int cols = std::max(1, static_cast<int>((W - 2 * margin + gap) / (tile_w + gap)));
  const float grid_w = cols * tile_w + (cols - 1) * gap, left = (W - grid_w) / 2;
  const int rows_visible = std::max(1, static_cast<int>((H - footer_h - top + gap) / (tile_h + gap)));
  const int sel_row = selected_ / cols;
  const int first_row = std::max(0, sel_row - rows_visible + 1);
  std::error_code ec;
  for (int i = first_row * cols; i < static_cast<int>(games_.size()); ++i) {
    const int row = i / cols - first_row, col = i % cols;
    if (row >= rows_visible) break;
    const Game& g = games_[i];
    const bool sel = i == selected_;
    const float x = left + col * (tile_w + gap), y = top + row * (tile_h + gap);
    if (sel) {
      ui_.rect(x - 10 * s, y - 10 * s, tile_w + 20 * s, tile_h + 20 * s, kRed.with_alpha(0.10f), 26 * s);
      ui_.gradient(x, y, tile_w, tile_h, rgb(0x3A1517), kTile, 18 * s);
      ui_.outline(x, y, tile_w, tile_h, kRed, 18 * s, 3 * s);
    } else {
      ui_.rect(x, y, tile_w, tile_h, kTile, 18 * s);
    }
    ui_.image(ui_.logo(), x + tile_w - 50 * s, y + 18 * s, 32 * s, 32 * s, sel ? 0.9f : 0.35f);
    const auto lines = wrap(ui_, g.title, 26 * s, tile_w - 90 * s, true, 3);
    for (std::size_t l = 0; l < lines.size(); ++l) {
      ui_.text(x + 22 * s, y + 22 * s + l * 32 * s, lines[l], 26 * s, sel ? kText : kText.with_alpha(0.85f), true);
    }
    std::string meta = g.region.empty() ? g.internal_name : g.region;
    ui_.text(x + 22 * s, y + tile_h - 44 * s, ui_.fit(meta, 18 * s, tile_w - 150 * s), 18 * s, kDim);
    if (fs::is_directory(library_dir_ / g.internal_name / "current", ec)) {
      const float cw = ui_.measure("Saved progress", 15 * s) + 24 * s;
      ui_.rect(x + tile_w - cw - 18 * s, y + tile_h - 48 * s, cw, 30 * s, kRed.with_alpha(0.18f), 15 * s);
      ui_.text(x + tile_w - cw / 2 - 18 * s, y + tile_h - 42 * s, "Saved progress", 15 * s, rgb(0xFF8F8A), false,
               Align::Center);
    }
  }
}

void App::draw_menu() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.55f));
  const auto items = menu_items();
  const float row = 50 * s, pw = 620 * s, ph = 150 * s + items.size() * row + 50 * s;
  const float px = (W - pw) / 2, py = std::max(20 * s, (H - ph) / 2);
  ui_.rect(px, py, pw, ph, kPanel.with_alpha(0.97f), 24 * s);
  ui_.outline(px, py, pw, ph, rgb(0xFFFFFF, 0.06f), 24 * s, 1.5f * s);
  ui_.image(ui_.logo(), px + 30 * s, py + 30 * s, 48 * s, 48 * s);
  ui_.text(px + 94 * s, py + 30 * s, ui_.fit(game_ ? game_->title : "", 28 * s, pw - 130 * s, true), 28 * s, kText, true);
  ui_.text(px + 94 * s, py + 66 * s, "Paused", 18 * s, kDim);
  float y = py + 120 * s;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const bool sel = i == menu_index_;
    if (sel) {
      ui_.rect(px + 18 * s, y, pw - 36 * s, row - 6 * s, kRed.with_alpha(0.16f), 12 * s);
      ui_.rect(px + 18 * s, y + 10 * s, 4 * s, row - 26 * s, kRed, 2 * s);
    }
    ui_.text(px + 40 * s, y + 11 * s, items[i].label, 21 * s, sel ? kText : kText.with_alpha(0.82f), sel);
    if (!items[i].value.empty()) {
      const bool adjustable = items[i].id >= kSlot && items[i].id <= kVolume;
      const std::string v = adjustable && sel ? "<  " + items[i].value + "  >" : items[i].value;
      ui_.text(px + pw - 40 * s, y + 11 * s, v, 21 * s, sel ? kText : kDim, false, Align::Right);
    }
    y += row;
  }
  ui_.text(px + pw / 2, py + ph - 42 * s, "Up/Down choose     Left/Right change     Enter / A select     Esc / B back", 16 * s,
           kFaint, false, Align::Center);
}

void App::draw_seating() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.65f));
  ui_.text(W / 2, H * 0.16f, "Who's playing?", 48 * s, kText, true, Align::Center);
  ui_.text(W / 2, H * 0.16f + 70 * s, "Controllers: press any button.   Keyboard: press Enter.", 22 * s, kDim, false,
           Align::Center);
  ui_.text(W / 2, H * 0.16f + 104 * s, "Two people on one keyboard: left player presses F, right player presses . (period)",
           20 * s, kFaint, false, Align::Center);
  const float cw = 380 * s, ch = 300 * s, gap = 30 * s, total = 4 * cw + 3 * gap;
  const float x0 = std::max(20 * s, (W - total) / 2), y0 = H * 0.36f;
  const float scale = std::min(1.0f, (W - 40 * s) / total);
  for (int i = 0; i < Input::kPlayers; ++i) {
    const Seat& seat = input_.seat(i);
    const float x = x0 + i * (cw + gap) * scale, w = cw * scale;
    ui_.rect(x, y0, w, ch, seat.empty() ? kPanel.with_alpha(0.9f) : kTile, 22 * s);
    if (!seat.empty()) ui_.outline(x, y0, w, ch, kPlayerColors[i], 22 * s, 3 * s);
    ui_.rect(x + w / 2 - 28 * s, y0 + 44 * s, 56 * s, 56 * s, seat.empty() ? kFaint.with_alpha(0.4f) : kPlayerColors[i], 28 * s);
    ui_.text(x + w / 2, y0 + 58 * s, std::to_string(i + 1), 28 * s, kText, true, Align::Center);
    ui_.text(x + w / 2, y0 + 130 * s, "Player " + std::to_string(i + 1), 28 * s, kText, true, Align::Center);
    const auto lines = wrap(ui_, seat.empty() ? "Press a button to join" : seat.name, 20 * s, w - 40 * s, false, 2);
    for (std::size_t l = 0; l < lines.size(); ++l) {
      ui_.text(x + w / 2, y0 + 180 * s + l * 28 * s, lines[l], 20 * s, seat.empty() ? kFaint : kDim, false, Align::Center);
    }
  }
  if (input_.keyboard_shared()) {
    ui_.text(W / 2, y0 + ch + 40 * s, "Two players share the keyboard. Many keyboards miss keys pressed together;", 20 * s,
             rgb(0xF2C12E), false, Align::Center);
    ui_.text(W / 2, y0 + ch + 70 * s, "gamepads work better, and stick-spinning minigames are much easier on one.", 20 * s,
             rgb(0xF2C12E), false, Align::Center);
  }
  ui_.text(W / 2, H - 70 * s, "F10, Esc or B when everyone has joined", 20 * s, kDim, false, Align::Center);
}

void App::draw_help() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.7f));
  const float pw = std::min(W - 40 * s, 1500 * s), ph = std::min(H - 40 * s, 820 * s);
  const float px = (W - pw) / 2, py = (H - ph) / 2;
  ui_.rect(px, py, pw, ph, kPanel, 24 * s);
  ui_.text(px + 40 * s, py + 32 * s, "Controls", 34 * s, kText, true);
  struct Row {
    const char* a;
    const char* b;
  };
  const Row keyboard[] = {
      {"Control stick", "Arrow keys (hold Left Shift to walk)"}, {"A / B", "X / C"}, {"Z trigger", "Z"},
      {"L / R", "A / S"}, {"Start", "Enter"}, {"C buttons", "I J K L"}, {"D-pad", "T F G H"},
  };
  const Row pad[] = {
      {"Control stick", "Left stick"}, {"A / B", "A / X"}, {"Z trigger", "Either trigger"}, {"L / R", "Bumpers"},
      {"Start", "Start"}, {"C buttons", "Right stick (or B and Y)"}, {"Menu", "Back or Guide"},
  };
  const Row hotkeys[] = {
      {"Esc", "Menu"}, {"F11 / Alt+Enter", "Fullscreen"}, {"F2", "Next fill mode"}, {"F3", "Next picture style"},
      {"F5 / F7", "Save / load state"}, {"F6", "Next state slot"}, {"F10", "Who's playing"}, {"F12", "Screenshot"},
      {"Ctrl+R", "Reset"},
  };
  auto column = [&](float x, const char* title, const Row* rows, int n) {
    float y = py + 100 * s;
    ui_.text(x, y, title, 22 * s, kRed, true);
    y += 44 * s;
    for (int i = 0; i < n; ++i) {
      ui_.text(x, y, rows[i].a, 19 * s, kDim);
      ui_.text(x + 170 * s, y, ui_.fit(rows[i].b, 19 * s, pw / 3 - 210 * s), 19 * s, kText);
      y += 34 * s;
    }
  };
  column(px + 40 * s, "Keyboard", keyboard, 7);
  column(px + pw / 3 + 20 * s, "Controller", pad, 7);
  column(px + 2 * pw / 3, "Shortcuts", hotkeys, 9);
  ui_.text(px + 40 * s, py + ph - 90 * s,
           "Two players on one keyboard: left uses WASD, F/G for A/B; right uses the arrows, . and , for A/B.", 18 * s, kDim);
  ui_.text(px + 40 * s, py + ph - 58 * s, "Your games save automatically into Documents\\N64 Saves, with the last 10 versions kept.",
           18 * s, kDim);
}

void App::draw_toasts() {
  const uint64_t now = SDL_GetTicksNS();
  while (!toasts_.empty() && toasts_.front().until_ns < now) toasts_.pop_front();
  const float s = out_h_ / 1080.0f;
  float y = out_h_ - (screen_ == Screen::Home ? 110 : 40) * s;
  for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
    const float left_ms = static_cast<float>(it->until_ns - now) / 1e6f;
    const float alpha = std::min(1.0f, left_ms / 400.0f);
    const std::string t = ui_.fit(it->text, 20 * s, out_w_ - 120 * s);
    const float w = ui_.measure(t, 20 * s) + 48 * s, h = 48 * s;
    y -= h + 10 * s;
    ui_.rect((out_w_ - w) / 2, y, w, h, rgb(0x121214, 0.92f * alpha), 14 * s);
    ui_.rect((out_w_ - w) / 2 + 14 * s, y + h / 2 - 4 * s, 8 * s, 8 * s, kRed.with_alpha(alpha), 4 * s);
    ui_.text((out_w_ - w) / 2 + 32 * s, y + 13 * s, t, 20 * s, kText.with_alpha(alpha));
  }
}

}  // namespace p64
