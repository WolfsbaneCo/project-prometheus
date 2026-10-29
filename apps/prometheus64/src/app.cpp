#include "app.hpp"
#include "app_shared.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <tuple>

#include "archive.hpp"
#include "net.hpp"
#include "savekit/formats.hpp"
#include "savekit/library.hpp"
#include "savekit/sha256.hpp"

namespace fs = std::filesystem;

namespace p64 {
namespace {

constexpr std::size_t kSrmSize = 0x48800;  // Mupen64Plus-Next save memory layout
constexpr const char* kOpt = "mupen64plus-";


struct PresetDef {
  const char* name;
  const char* blurb;
  const char* rdp;  // renderer: GLideN64 (enhanced) or Angrylion (pixel-exact software)
  const char* rsp;
  int factor;  // 0 = match the display
  const char* bilinear;
  bool dither;
  int fxaa, msaa;
  int textures;  // default TextureLook
  float sharpen, scanlines;
  bool smooth;
};
// Texture enhancement (GLideN64). xBRZ redraws the game's small textures at 4x with clean edges.
enum TextureLook { kTexCrisp, kTexSoft, kTexOriginal, kTexCount };
const char* const kTextureNames[kTexCount] = {"HD (crisp)", "HD (soft)", "Original"};
const char* const kTextureModes[kTexCount] = {"4xBRZ", "HQ4X", "None"};
const PresetDef kPresets[] = {
    {"Clean", "Screen resolution, HD textures, smooth edges", "gliden64", "hle", 0, "3point", false, 0, 4, kTexCrisp, 0.0f,
     0.0f, true},
    {"Sharp", "Clean, with extra crispness", "gliden64", "hle", 0, "3point", false, 0, 4, kTexCrisp, 0.3f, 0.0f, true},
    {"Retro", "CRT-style scanlines", "gliden64", "hle", 2, "3point", false, 0, 0, kTexOriginal, 0.0f, 0.65f, true},
    {"Native", "Pixel-exact original picture (uses more CPU)", "angrylion", "parallel", 1, "3point", true, 0, 0,
     kTexOriginal, 0.0f, 0.0f, false},
};


// A Controller Pak holds a game's data when its note table (pages 3-4) names a game.
// Games whose 2D parts (text, text boxes, fades) are drawn for a 4:3 screen, so the widescreen
// trick, which widens only the 3D world, cuts and misplaces them. Harvest Moon 64: a fade covers
// only the middle, letters go missing from the yes/no choices. By the name in the game's header.
bool widescreen_breaks(const std::string& internal_name) {
  static const char* const kNames[] = {"HARVESTMOON64"};
  for (const char* n : kNames) {
    if (internal_name == n) return true;
  }
  return false;
}

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

}  // namespace

const char* preset_name(Preset p) { return kPresets[static_cast<int>(p)].name; }

App::App(Options options) : opt_(std::move(options)) {}

int App::run() {
  std::string error;
  if (!init(error)) {
    SDL_Log("Project Prometheus could not start: %s", error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Project Prometheus", error.c_str(), window_);
    shutdown();
    return 1;
  }
  loop();
  shutdown();
  return 0;
}

bool App::init(std::string& error) {
  SDL_SetAppMetadata("Project Prometheus", P64_VERSION, "org.projectprometheus.app");
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    error = SDL_GetError();
    return false;
  }
  user_event_ = SDL_RegisterEvents(1);

  // The app was called Prometheus 64 at first: its settings, online key and backup state stay in
  // that folder, so they carry over.
  if (char* pref = SDL_GetPrefPath("Project Prometheus", "Prometheus 64")) {
    pref_dir_ = from_utf8(pref);
    SDL_free(pref);
  }
  assets_dir_ = from_utf8(SDL_GetBasePath() ? SDL_GetBasePath() : "") / "assets";
  settings_.load(pref_dir_ / "settings.ini");
  load_group_key();
  // Saves for every console share one folder: Memories\N64, Memories\GBA.
  // Earlier versions used Documents\N64 Saves; a computer that has it keeps using it (nothing moves).
  const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  const fs::path docs_dir = docs ? from_utf8(docs) : pref_dir_;
  std::error_code lib_ec;
  const fs::path default_library = fs::is_directory(docs_dir / "N64 Saves", lib_ec) ? docs_dir / "N64 Saves"
                                                                                     : docs_dir / "Memories" / "N64";
  library_dir_ = from_utf8(settings_.get("library", utf8(default_library)));
  // GBA saves sit next to the N64 ones (Memories\GBA), or in Documents\Memories\GBA when the N64
  // library is somewhere else (the earlier N64 Saves folder).
  const fs::path default_gba = library_dir_.filename() == "N64" ? library_dir_.parent_path() / "GBA"
                                                                 : docs_dir / "Memories" / "GBA";
  gba_library_dir_ = from_utf8(settings_.get("gba_library", utf8(default_gba)));
  slot_ = std::clamp(settings_.get_int("state_slot", 1), 1, 10);

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  window_ = SDL_CreateWindow("Project Prometheus", opt_.width, opt_.height,
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
  // One emulator core per console, in "cores" next to the program.
  const fs::path base = from_utf8(SDL_GetBasePath() ? SDL_GetBasePath() : "");
#if defined(_WIN32)
  const char* ext = ".dll";
#elif defined(__APPLE__)
  const char* ext = ".dylib";
#else
  const char* ext = ".so";
#endif
  n64_core_path_ = opt_.core.empty() ? base / "cores" / (std::string("mupen64plus_next_libretro") + ext) : opt_.core;
  gba_core_path_ = base / "cores" / (std::string("gpsp_libretro") + ext);
  const fs::path core_path = n64_core_path_;
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
      static double low = 1e9, high = 0;
      low = std::min(low, audio_.queued_seconds());
      high = std::max(high, audio_.queued_seconds());
      for (std::size_t i = 0; i < n * 2; ++i) {
        const uint64_t a = static_cast<uint64_t>(std::abs(static_cast<int>(d[i])));
        peak = std::max(peak, a);
        loud += a > 512;
      }
      if (frames == 0 || (frames + n) / 44100 != frames / 44100) {
        SDL_Log("audio: %llu frames so far, %llu loud samples, peak %llu, queued %.3f-%.3fs", static_cast<unsigned long long>(frames + n),
                static_cast<unsigned long long>(loud), static_cast<unsigned long long>(peak), low, high);
        low = 1e9;
        high = 0;
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
    if (net_.running() && online_game_) {
      // Online, a port belongs to one computer: rumble the controller of the local player on it.
      if (port >= Netplay::kPorts) return false;
      const Netplay::Port& p = net_.ports()[port];
      const auto seats = local_seats();
      if (p.player != net_.my_id() || p.local >= static_cast<int>(seats.size())) return false;
      port = static_cast<unsigned>(seats[p.local]);
    }
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
  loaded_system_ = System::N64;
  error = core_.open(core_path);
  if (!error.empty()) {
    error += "\n\nExpected the emulator core at:\n" + utf8(core_path) + "\n\nReinstalling Project Prometheus fixes this.";
    return false;
  }

  input_.init();
  input_.load_bindings(settings_);
  net::init();
  init_backup();
  rescan();
  set_fullscreen(opt_.fullscreen || settings_.get_int("fullscreen", 0) == 1);
  if (opt_.rom.extension() == ".prometheuskey") {
    // A key file double-clicked in File Explorer: add it, then show online play.
    if (import_key_file(opt_.rom)) open_online();
    opt_.rom.clear();
  }
  if (!opt_.rom.empty()) {
    std::string why;
    auto found = read_games(opt_.rom, &why);
    if (found.empty()) {
      error = why.empty() ? "No N64 game found in " + utf8(opt_.rom) : "Could not read " + utf8(opt_.rom) + ": " + why;
      return false;
    }
    if (opt_.online_host) {
      hosted_game_ = found.front();  // test mode: host it online instead of playing it here
      if (games_.empty()) games_ = found;
      open_online();
      host_online();
    } else if (!start_game(found.front())) {
      error = "could not start " + utf8(opt_.rom);
      return false;
    }
  }
  if (!opt_.save_key.empty()) save_key_file(opt_.save_key);
  if (!opt_.setup_token.empty()) {
    open_online();
    online_page_ = OnlinePage::Setup;
    token_input_ = opt_.setup_token;
    start_setup();
  }
  if (!opt_.online_join.empty()) {
    open_online();
    code_input_ = opt_.online_join;
    join_online();
  }
  return true;
}

void App::shutdown() {
  net_.leave();
  if (setup_thread_.joinable()) setup_thread_.join();
  if (game_) stop_game();
  backup_on_exit();
  net::shutdown();
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

int App::resolution_factor(const std::string& id) const {
  const Preset preset = static_cast<Preset>(std::clamp(settings_.game_int(id, "preset", 0), 0, 3));
  const int fixed = kPresets[static_cast<int>(preset)].factor;
  if (fixed > 0) return fixed;
  const int chosen = settings_.game_int(id, "resolution", 0);
  if (chosen >= 1 && chosen <= 8) return chosen;
  return 0;  // auto or super: render at an exact size (render_height)
}

bool App::supersampling(const std::string& id) const {
  return kPresets[std::clamp(settings_.game_int(id, "preset", 0), 0, 3)].factor == 0 &&
         settings_.game_int(id, "resolution", 0) == kResSuper;
}

int App::render_height(const std::string& id) const {
  // The screen's full height, so in fullscreen every game pixel lands on exactly one screen pixel.
  int h = 1080;
  if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(window_))) h = m->h;
  h = std::clamp(h, 240, 2880);
  // Super: twice the screen, shrunk by exactly half when shown (a clean 2x2 average per pixel).
  return supersampling(id) ? std::min(h * 2, 4320) : h;
}

int App::texture_look(const std::string& id) const {
  const PresetDef& p = kPresets[std::clamp(settings_.game_int(id, "preset", 0), 0, 3)];
  if (std::string(p.rdp) != "gliden64") return kTexOriginal;
  const int t = settings_.game_int(id, "textures", -1);
  return t >= 0 && t < kTexCount ? t : p.textures;
}

void App::apply_core_options(const Game& game) {
  if (game.system == System::GBA) {
    auto set = [&](const char* key, const std::string& value) { core_.set_option(key, value); };
    set("gpsp_bios", "auto");        // the player's own gba_bios.bin in the system folder, else the open BIOS
    set("gpsp_boot_mode", "game");   // straight into the game, like inserting a cartridge
    set("gpsp_drc", "enabled");
    set("gpsp_rtc", "auto");         // the cartridge clock, for the games that have one
    set("gpsp_rtc_time_source", "system");  // follows the real time, as a cartridge's battery clock does
    set("gpsp_serial", "auto");      // link play: picks the cable mode or the Wireless Adapter by game
    set("gpsp_rumble", "1");
    set("gpsp_frameskip", "disabled");
    for (const auto& o : opt_.core_options) {  // test mode: "--option key=value" overrides
      const auto eq = o.find('=');
      if (eq != std::string::npos) set(o.substr(0, eq).c_str(), o.substr(eq + 1));
    }
    return;
  }
  const PresetDef& p = kPresets[std::clamp(settings_.game_int(game.id, "preset", 0), 0, 3)];
  const bool widescreen = fill_mode(&game) == static_cast<int>(FillMode::Widescreen);
  auto set = [&](const char* key, const std::string& value) { core_.set_option(std::string(kOpt) + key, value); };
  set("rdp-plugin", settings_.get("renderer", p.rdp));
  set("rsp-plugin", settings_.get("renderer", p.rdp) == std::string("gliden64") ? "hle" : p.rsp);
  set("cpucore", "dynamic_recompiler");
  set("EnableNativeResFactor", std::to_string(resolution_factor(game.id)));
  // Factor 0 renders at an exact size: the screen's height, at the picture's shape.
  const int rh = render_height(game.id), rw43 = (rh * 4 / 3 + 1) / 2 * 2, rw169 = (rh * 16 / 9 + 1) / 2 * 2;
  set("43screensize", std::to_string(rw43) + "x" + std::to_string(rh));
  set("169screensize", std::to_string(rw169) + "x" + std::to_string(rh));
  set("BilinearMode", p.bilinear);
  set("DitheringPattern", p.dither ? "True" : "False");
  set("DitheringQuantization", p.dither ? "True" : "False");
  set("RDRAMImageDitheringMode", p.dither ? "BlueNoise" : "False");
  set("FXAA", std::to_string(p.fxaa));
  set("MultiSampling", supersampling(game.id) ? "0" : std::to_string(p.msaa));  // supersampling smooths edges already
  const int tex = texture_look(game.id);
  set("txEnhancementMode", kTextureModes[tex]);
  set("txFilterMode", "None");
  set("txFilterIgnoreBG", tex == kTexOriginal ? "True" : "False");
  set("CorrectTexrectCoords", "Auto");  // no thin gaps between the pieces of 2D images
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
  if (online_game_) {
    // Online, every computer must compute exactly the same game: the same renderer, and nothing
    // read back from the graphics card into the console's memory (it can differ between cards).
    set("rdp-plugin", "gliden64");
    set("rsp-plugin", "hle");
    set("EnableCopyColorToRDRAM", "Off");
    set("EnableCopyDepthToRDRAM", "Software");
    for (int i = 0; i < Netplay::kPorts; ++i) {
      set(("pak" + std::to_string(i + 1)).c_str(), net_.pak(i) == 2 ? "rumble" : "memory");
    }
  }
  // Test mode: "--option key=value" overrides, for comparing renderer settings.
  for (const auto& o : opt_.core_options) {
    const auto eq = o.find('=');
    if (eq != std::string::npos) set(o.substr(0, eq).c_str(), o.substr(eq + 1));
  }
}

std::string App::use_core(System system) {
  if (system == loaded_system_ && core_.info().library_name) return {};
  core_.close();
  const fs::path& path = system == System::GBA ? gba_core_path_ : n64_core_path_;
  std::string err = core_.open(path);
  if (!err.empty()) {
    // Put the previous core back, so the app stays usable.
    core_.open(loaded_system_ == System::GBA ? gba_core_path_ : n64_core_path_);
    return std::string("the ") + system_name(system) + " emulator core is missing (" + utf8(path) +
           "). Reinstalling Project Prometheus fixes this";
  }
  loaded_system_ = system;
  return {};
}

bool App::start_game(const Game& game) {
  if (game_) stop_game();
  if (const std::string err = use_core(game.system); !err.empty()) {
    toast("Could not start " + game.title + ": " + err, 8);
    return false;
  }
  input_.set_gba(game.system == System::GBA);
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
  if (guest_session_) {
    // Online guest: play from the host's save, and never write it into this computer's library.
    last_saved_ram_.clear();
    pending_ram_.clear();
    if (uint8_t* ram = core_.save_ram(); ram && guest_save_.size() == core_.save_ram_size()) {
      std::memcpy(ram, guest_save_.data(), guest_save_.size());
    }
  } else {
    load_game_save();
  }
  screen_ = Screen::Playing;
  overlay_ = Overlay::None;
  acc_ns_ = 0;
  last_ns_ = SDL_GetTicksNS();
  speed_since_ns_ = 0;
  slow_spans_ = 0;
  slow_told_ = false;
  settings_.set("last_game", utf8(game.path));
  if (!online_game_) toast(game.title + "  -  Esc, right-click or Back on a controller opens the menu", 5);
  if (input_.keyboard_shared()) toast("Two players share the keyboard: gamepads work better", 5);
  return true;
}

void App::stop_game() {
  if (!game_) return;
  sync_game_save(true);
  guest_session_ = false;
  online_game_ = false;
  guest_save_.clear();
  core_.context_destroy();
  core_.close();
  // Between games the N64 core is loaded (a core can't start a second game in one session).
  loaded_system_ = System::N64;
  if (const std::string err = core_.open(n64_core_path_); !err.empty()) toast(err, 8);
  input_.set_gba(false);
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
  if (!core_.open(game.system == System::GBA ? gba_core_path_ : n64_core_path_).empty()) {
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

int App::fill_mode(const Game* game) const {
  const int f = std::clamp(settings_.game_int(game ? game->id : "", "fill", 0), 0, static_cast<int>(FillMode::Count) - 1);
  const bool breaks = game && widescreen_breaks(game->internal_name);
  return f == static_cast<int>(FillMode::Widescreen) && breaks ? static_cast<int>(FillMode::Fit) : f;
}

Look App::current_look() const {
  Look l;
  const std::string id = game_ ? game_->id : "";
  const PresetDef& p = kPresets[std::clamp(settings_.game_int(id, "preset", 0), 0, 3)];
  l.fill = static_cast<FillMode>(fill_mode(game_ ? &*game_ : nullptr));
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
  if (game_->system == System::GBA) {
    if (!ram || core_.save_ram_size() != savekit::kGbaSaveSize) return;
    try {
      if (auto saved = savekit::load_current(gba_library_dir_, game_->internal_name)) {
        auto it = saved->memories.find(savekit::Memory::GbaSave);
        if (it != saved->memories.end() && it->second.size() == savekit::kGbaSaveSize) {
          std::memcpy(ram, it->second.data(), savekit::kGbaSaveSize);
          toast("Loaded your saved progress");
        }
      }
    } catch (const std::exception& e) {
      toast(std::string("Could not load the save: ") + e.what(), 6);
    }
    last_saved_ram_.assign(ram, ram + savekit::kGbaSaveSize);
    return;
  }
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
  if (guest_session_) return;  // the host keeps the save of an online game
  uint8_t* ram = core_.save_ram();
  const bool gba = game_ && game_->system == System::GBA;
  const std::size_t size = gba ? savekit::kGbaSaveSize : kSrmSize;
  if (!game_ || !ram || core_.save_ram_size() != size) return;
  std::vector<uint8_t> now(ram, ram + size);
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
    o.library = library_for(game_->system);
    o.game = game_->internal_name;
    if (gba) {
      // Never written (all 0xFF, as the core starts it): nothing to keep.
      if (std::all_of(now.begin(), now.end(), [](uint8_t b) { return b == 0xFF; })) {
        last_saved_ram_ = now;
        return;
      }
      o.save.memories[savekit::Memory::GbaSave] = savekit::Bytes(now);
    } else {
      o.save = savekit::decode(savekit::Format::RetroArchMupen64PlusNext, {{".srm", savekit::Bytes(now)}}).save;
    }
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
  return library_for(game_->system) / game_->internal_name / "states" / ("slot" + std::to_string(slot) + ".state");
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

// How many screen refreshes make one game frame when the two line up (a 60 Hz screen: 1, 120 Hz:
// 2), or 0 when they don't (75 Hz, 144 Hz) or the screen isn't keeping up.
int App::refreshes_per_frame(double fps) const {
  if (!vsync_working_ || refresh_ns_ <= 0 || unlocked_for_ > 0) return 0;
  const double per = 1e9 / fps / refresh_ns_;
  const int n = static_cast<int>(std::lround(per));
  return n >= 1 && n <= 4 && std::abs(per / n - 1.0) < 0.02 ? n : 0;
}

void App::run_frames() {
  const uint64_t now = SDL_GetTicksNS();
  uint64_t elapsed = now - last_ns_;
  // With vsync, count whole refreshes rather than the clock's jitter, so frames come at an even
  // rhythm (on a 144 Hz screen: the same pattern of 2 and 3 refreshes per frame, always).
  if (vsync_working_ && refresh_ns_ > 0 && elapsed < refresh_ns_ * 1.5) elapsed = static_cast<uint64_t>(refresh_ns_);
  acc_ns_ += elapsed;
  last_ns_ = now;
  const double fps = core_.av().timing.fps > 1 ? core_.av().timing.fps : 60.0;
  const uint64_t frame_ns = static_cast<uint64_t>(1e9 / fps);
  if (acc_ns_ > 4 * frame_ns) acc_ns_ = frame_ns;  // after a pause or a hitch, don't race to catch up
  if (net_game_pending_ || (online_game_ && !net_.running())) {
    acc_ns_ = 0;  // loaded, waiting for everyone else
    return;
  }
  if (net_.running() && online_game_) {
    audio_.set_speed(1.0);  // online games keep the N64's own clock, the same on every computer
    run_online_frames(frame_ns);  // the host's menu pauses everyone, so it decides
    return;
  }
  if (screen_ != Screen::Playing || overlay_ != Overlay::None) {
    acc_ns_ = 0;
    speed_since_ns_ = 0;
    return;
  }
  if (!opt_.screenshot.empty()) acc_ns_ = frame_ns;  // test mode: one game frame per drawn frame, so runs repeat exactly
  // When the screen's refresh lines up with the game (it nearly always does: 60 Hz), run exactly
  // one frame every n refreshes. Going by the clock instead, the two drift across each other and
  // the game now and then shows a frame twice and then skips one: a small, regular stutter. The
  // game then runs at the screen's speed (60 Hz against the N64's 59.94: 0.1% faster), and the
  // sound follows along.
  if (const int n = opt_.screenshot.empty() ? refreshes_per_frame(fps) : 0) {
    acc_ns_ = 0;
    audio_.set_speed(1e9 / (refresh_ns_ * n) / fps);
    if (++refreshes_ % static_cast<uint64_t>(n) == 0) {
      core_.run();
      ++frame_count_;
      if (frame_count_ % 30 == 0) sync_game_save(false);
    }
    check_speed(fps);
    return;
  }
  audio_.set_speed(1.0);
  for (int runs = 0; acc_ns_ >= frame_ns && runs < 3; ++runs) {
    core_.run();
    acc_ns_ -= frame_ns;
    ++frame_count_;
    if (frame_count_ % 30 == 0) sync_game_save(false);
  }
  check_speed(fps);
}

// Whether this computer keeps the game at full speed: game frames are counted over 5-second
// stretches of play, and if clearly fewer ran than the game's speed, the player hears once (per
// game) which settings are lighter. Slow settings are the usual cause of a game that drags.
void App::check_speed(double fps) {
  if (!opt_.screenshot.empty() || slow_told_) return;
  const uint64_t now = SDL_GetTicksNS();
  if (!speed_since_ns_) {
    speed_since_ns_ = now;
    speed_frames_ = frame_count_;
    return;
  }
  const uint64_t span = now - speed_since_ns_;
  if (span < 5'000'000'000ULL) return;
  const double expected = fps * static_cast<double>(span) / 1e9;
  slow_spans_ = static_cast<double>(frame_count_ - speed_frames_) < expected * 0.95 ? slow_spans_ + 1 : 0;
  if (slow_spans_ >= 2) {
    slow_told_ = true;
    SDL_Log("speed: %llu frames in %.1f s, expected %.0f", static_cast<unsigned long long>(frame_count_ - speed_frames_),
            static_cast<double>(span) / 1e9, expected);
    toast("This computer can't keep the game at full speed with these settings. Menu > Picture: Retro, or "
          "Textures: Original, is lighter",
          9);
  }
  speed_since_ns_ = now;
  speed_frames_ = frame_count_;
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

    // Test mode: left clicks ("frame:x,y" in pixels, ",2" for a double click), sent like real mouse events.
    for (const auto& c : opt_.clicks) {
      const auto colon = c.find(':'), comma = c.find(','), comma2 = c.find(',', comma + 1);
      if (std::stoull(c.substr(0, colon)) != frames_drawn) continue;
      const float d = SDL_GetWindowPixelDensity(window_) > 0 ? SDL_GetWindowPixelDensity(window_) : 1.0f;
      const float x = std::stof(c.substr(colon + 1, comma - colon - 1)) / d, y = std::stof(c.substr(comma + 1)) / d;
      SDL_Event m{};
      m.type = SDL_EVENT_MOUSE_MOTION;
      m.motion.x = x;
      m.motion.y = y;
      handle(m);
      m = {};
      m.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
      m.button.button = SDL_BUTTON_LEFT;
      m.button.x = x;
      m.button.y = y;
      m.button.down = true;
      m.button.clicks = comma2 == std::string::npos ? 1 : static_cast<Uint8>(std::stoi(c.substr(comma2 + 1)));
      handle(m);
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
    process_net_events();
    process_backup();
    if (game_) run_frames();
    audio_.set_paused(!game_ || (overlay_ != Overlay::None && !(online_game_ && !net_.is_host())));
    draw();
    ++frames_drawn;

    if (game_ && opt_.poke_frame > 0 && frames_drawn == static_cast<uint64_t>(opt_.poke_frame)) {
      // Test mode: the "game" writes to its SRAM (offset 0x20800 in the core's save layout).
      if (uint8_t* ram = core_.save_ram(); ram && core_.save_ram_size() == kSrmSize) {
        std::string text = opt_.poke_text.empty() ? std::string("PROMETHE") : opt_.poke_text;
        text.resize(8, ' ');
        for (int w = 0; w < 8; w += 4) std::reverse(text.begin() + w, text.begin() + w + 4);  // the core's word order
        std::memcpy(ram + 0x20800, text.data(), 8);
      }
    }
    if (!opt_.screenshot.empty() && !opt_.online_exit_frame && static_cast<int>(frames_drawn) >= opt_.frames) {
      if (!opt_.dump_save.empty() && core_.save_ram()) {
        std::ofstream(opt_.dump_save, std::ios::binary)
            .write(reinterpret_cast<const char*>(core_.save_ram()), static_cast<std::streamsize>(core_.save_ram_size()));
      }
      if (opt_.ram_hash && core_.system_ram()) {
        const savekit::Bytes ram(core_.system_ram(), core_.system_ram() + core_.system_ram_size());
        std::printf("ram-sha256 %s\n", savekit::sha256_hex(ram).c_str());
        std::fflush(stdout);
      }
      take_screenshot(opt_.screenshot);
      running_ = false;
    }
    const uint64_t t1 = SDL_GetTicksNS();
    SDL_GL_SwapWindow(window_);
    swap_ns_total += SDL_GetTicksNS() - t1;
    // With working vsync the swap paces the loop; otherwise sleep until the next frame is due.
    if (frames_drawn == 30) vsync_working_ = swap_ns_total / 30 > 3'000'000;
    if (vsync_working_) {
      // Wait for the picture to be on screen. Otherwise the driver can queue up to 3 frames ahead,
      // and every one of them is a frame between pressing a button and seeing it happen.
      gl::glFinish();
      // Measure the refresh period (a missed refresh, from a slow frame, doesn't count; it
      // unlocks the pacing for a while instead, so a slow PC keeps the game at full speed).
      const uint64_t now = SDL_GetTicksNS();
      const double dt = last_swap_ns_ ? static_cast<double>(now - last_swap_ns_) : 0;
      last_swap_ns_ = now;
      if (dt > 0 && refresh_ns_ <= 0) {
        if (const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)); m && m->refresh_rate > 1) {
          refresh_ns_ = 1e9 / m->refresh_rate;
        } else {
          refresh_ns_ = dt;
        }
      } else if (dt > 0 && dt < refresh_ns_ * 1.5 && dt > refresh_ns_ * 0.5) {
        refresh_ns_ += (dt - refresh_ns_) * 0.02;
        if (unlocked_for_ > 0) --unlocked_for_;
      } else if (dt >= refresh_ns_ * 1.5) {
        unlocked_for_ = 120;
      }
    }
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
    } else if (e.user.code == kKeyChosen) {
      if (import_key_file(from_utf8(path)) && overlay_ != Overlay::Online) open_online();
    } else if (e.user.code == kKeySaveAs) {
      save_key_file(from_utf8(path));
    }
    return;
  }
  if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) {
    const fs::path file = from_utf8(e.drop.data);
    if (file.extension() == ".prometheuskey") {
      if (import_key_file(file) && !game_) open_online();
      return;
    }
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
      opt_.screenshot.empty() && !online_game_) {
    overlay_ = Overlay::Menu;  // pause when the window loses focus
    menu_index_ = 0;
  }

  // The Controls screen waiting for a key or button gets it before anything else can act on it.
  if (overlay_ == Overlay::Controls && controls_capture_ && handle_capture(e)) return;
  if (handle_text(e)) return;
  if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_WHEEL) {
    handle_mouse(e);
    return;
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
        const fs::path f = from_utf8(pics) / "Project Prometheus" / ("screenshot-" + std::to_string(stamp) + ".png");
        take_screenshot(f);
        toast("Screenshot saved to " + utf8(f.parent_path()));
      }
      return true;
    default:
      break;
  }
  // The game's shortcuts work while playing and in its menu (which shows them).
  if (!game_ || (overlay_ != Overlay::None && overlay_ != Overlay::Menu)) return false;
  switch (e.key.scancode) {
    case SDL_SCANCODE_F8:
    case SDL_SCANCODE_F9:
      menu_change(kVolume, e.key.scancode == SDL_SCANCODE_F9 ? 1 : -1);
      settings_.save();
      toast("Volume " + std::to_string(settings_.get_int("volume", 80)) + "%", 2);
      return true;
    case SDL_SCANCODE_F5:
      if (!online_blocks(kSaveState)) save_state();
      return true;
    case SDL_SCANCODE_F7:
      if (!online_blocks(kLoadState)) load_state();
      return true;
    case SDL_SCANCODE_R:
      if (!ctrl) return false;
      if (online_blocks(kReset)) return true;
      core_.reset();
      toast("Reset");
      return true;
    default:
      return false;
  }
}

void App::handle_home(const SDL_Event& e) {
  if (e.type == SDL_EVENT_KEY_DOWN) {
    // Arrows, WASD or the number pad all move, for keyboards with missing or broken arrow keys.
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: case SDL_SCANCODE_W: case SDL_SCANCODE_KP_8: ui_action(kUp); break;
      case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: case SDL_SCANCODE_KP_2: ui_action(kDown); break;
      case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: case SDL_SCANCODE_KP_4: ui_action(kLeft); break;
      case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: case SDL_SCANCODE_KP_6: ui_action(kRight); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: ui_action(kAccept); break;
      case SDL_SCANCODE_O: choose_games_folder(); break;
      case SDL_SCANCODE_I: choose_save_to_import(); break;
      case SDL_SCANCODE_C: open_controls(); break;
      case SDL_SCANCODE_N: open_online(); break;
      case SDL_SCANCODE_B: open_backup(); break;
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
      case SDL_GAMEPAD_BUTTON_WEST: open_online(); break;
      default: break;
    }
  }
}

void App::handle_menu(const SDL_Event& e) {
  if (e.type == SDL_EVENT_KEY_DOWN) {
    switch (e.key.scancode) {
      case SDL_SCANCODE_UP: case SDL_SCANCODE_W: case SDL_SCANCODE_KP_8: ui_action(kUp); break;
      case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: case SDL_SCANCODE_KP_2: ui_action(kDown); break;
      case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: case SDL_SCANCODE_KP_4: ui_action(kLeft); break;
      case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: case SDL_SCANCODE_KP_6: ui_action(kRight); break;
      case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_SPACE: ui_action(kAccept); break;
      case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: ui_action(kBack); break;
      case SDL_SCANCODE_Q: ui_action(kPrevTab); break;
      case SDL_SCANCODE_DELETE:
        if (overlay_ == Overlay::Seating) input_.clear_seats();
        break;
      case SDL_SCANCODE_E: case SDL_SCANCODE_TAB: ui_action(kNextTab); break;
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
      case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: ui_action(kPrevTab); break;
      case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: ui_action(kNextTab); break;
      default: break;
    }
  }
}

void App::ui_action(int action) {
  if (overlay_ == Overlay::Controls) {
    controls_action(action);
    return;
  }
  if (overlay_ == Overlay::Online) {
    online_action(action);
    return;
  }
  if (overlay_ == Overlay::Backup) {
    backup_action(action);
    return;
  }
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
  const int rh = render_height(id), rw = look.fill == FillMode::Widescreen ? rh * 16 / 9 : rh * 4 / 3;
  std::string res = kPresets[preset].factor > 0     ? "Set by the picture style"
                    : res_choice == kResSuper        ? "Super (" + std::to_string(rw) + "x" + std::to_string(rh) + ")"
                    : res_choice >= 1 && res_choice <= 8 ? std::to_string(res_choice) + "x"
                                                     : "Auto (" + std::to_string(rw) + "x" + std::to_string(rh) + ")";
  const bool gliden = std::string(kPresets[preset].rdp) == "gliden64";
  const bool online = online_game_ && net_.active();
  return {
      {"Resume", "", kResume, "Esc"},
      {"Save state", online ? "off online" : "slot " + std::to_string(slot_), kSaveState, "F5"},
      {"Load state", online ? "off online" : "slot " + std::to_string(slot_), kLoadState, "F7"},
      {"State slot", std::to_string(slot_), kSlot},
      {"Picture", kPresets[preset].name, kPicture},
      {"Textures", gliden ? kTextureNames[texture_look(id)] : "Set by the picture style", kTextures},
      {"Fill mode", fill_mode_name(look.fill), kFill},
      {"Resolution", res, kResolution},
      {"Overscan crop", std::to_string(static_cast<int>(std::lround(look.overscan * 100))) + "%", kOverscan},
      {"Volume", std::to_string(settings_.get_int("volume", 80)) + "%", kVolume, "F8/F9"},
      {"Fullscreen", fullscreen_ ? "On" : "Off", kFullscreenItem, "F11"},
      {"Who's playing", std::to_string(input_.seated_count()) + (input_.seated_count() == 1 ? " player" : " players"), kPlayers, "F10"},
      {"Controls", "", kControlsItem},
      {online_game_ && !net_.is_host() ? "Reset game (host only)" : online_game_ ? "Reset game for everyone" : "Reset game", "", kReset, "Ctrl+R"},
      {!online_game_ ? "Quit to library" : net_.is_host() ? "End online game" : "Leave online game", "", kQuit},
  };
}

void App::menu_change(int id, int dir) {
  const std::string gid = game_ ? game_->id : "";
  const int step = dir == 0 ? 1 : dir;
  // Online, some actions would put the players out of sync (or need the host).
  const bool acts = dir == 0 || id == kPicture || id == kTextures || id == kResolution;
  if (acts && id != kResume && online_blocks(id)) {
    settings_.save();
    return;
  }
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
    case kTextures: {
      if (std::string(kPresets[std::clamp(settings_.game_int(gid, "preset", 0), 0, 3)].rdp) != "gliden64") break;
      const int t = (texture_look(gid) + step + kTexCount) % kTexCount;
      settings_.set_game_int(gid, "textures", t);
      toast(std::string("Textures: ") + kTextureNames[t]);
      restart_game_keep_progress();
      break;
    }
    case kFill: {
      const int count = static_cast<int>(FillMode::Count);
      const int ws = static_cast<int>(FillMode::Widescreen);
      const int before = fill_mode(game_ ? &*game_ : nullptr);
      int f = (before + step + count) % count;
      const bool skip = f == ws && game_ && widescreen_breaks(game_->internal_name);
      if (skip) f = (f + step + count) % count;
      settings_.set_game_int(gid, "fill", f);
      if (skip) {
        toast("No widescreen for this game: its text and fades are made for a 4:3 screen. Ambient fill fills the sides instead", 6);
      } else {
        toast(std::string("Fill mode: ") + fill_mode_name(static_cast<FillMode>(f)) +
              (static_cast<FillMode>(f) == FillMode::Widescreen ? "  (text can look wrong in some games: then choose another)" : ""));
      }
      if ((before == ws) != (f == ws)) {
        if (online_game_ && net_.active()) {
          // Turning widescreen on or off restarts the game, which an online game can't do.
          settings_.set_game_int(gid, "fill", before);
          toast("Widescreen goes on or off before going online: it restarts the game", 4);
          break;
        }
        restart_game_keep_progress();
      }
      break;
    }
    case kResolution: {
      // Order in the menu: Auto, Super, then fixed 1x to 8x.
      static const int order[] = {0, kResSuper, 1, 2, 3, 4, 5, 6, 7, 8};
      constexpr int n = static_cast<int>(std::size(order));
      const int cur = settings_.game_int(gid, "resolution", 0);
      int at = 0;
      for (int i = 0; i < n; ++i) {
        if (order[i] == cur) at = i;
      }
      const int r = order[(at + step + n) % n];
      settings_.set_game_int(gid, "resolution", r);
      if (r == kResSuper) toast("Super: drawn at twice the screen size for the smoothest edges. Needs a strong graphics card", 5);
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
    case kControlsItem:
      if (dir == 0) open_controls();
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
  hits_.clear();
  drawing_overlay_ = false;
  if (screen_ == Screen::Home) draw_home();
  const bool playing = screen_ == Screen::Playing && overlay_ == Overlay::None;
  if (playing && mouse_recent()) {
    // The mouse moved during a game: offer the menu, for players without a free key or button.
    const float s = out_h_ / 1080.0f;
    draw_button(out_w_ - 32 * s, 28 * s, "Menu", s, false, false, kHitMenuButton, 0, true);
  }
  drawing_overlay_ = true;
  if (overlay_ == Overlay::Menu) draw_menu();
  if (overlay_ == Overlay::Seating) draw_seating();
  if (overlay_ == Overlay::Help) draw_help();
  if (overlay_ == Overlay::Controls) draw_controls();
  if (overlay_ == Overlay::Online) draw_online();
  if (overlay_ == Overlay::Backup) draw_backup();
  draw_online_status();
  if (overlay_ == Overlay::None || overlay_ == Overlay::Online || overlay_ == Overlay::Backup) draw_toasts();  // messages wait while a menu is open
  ui_.end();
  if (!playing || mouse_recent()) {
    SDL_ShowCursor();
  } else {
    SDL_HideCursor();
  }
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
  ui_.text(margin + 92 * s, 52 * s, "Project Prometheus", 44 * s, kText, true);
  ui_.text(margin + 94 * s, 106 * s, "Classics, rekindled", 21 * s, kDim);

  // Footer: every shortcut is also a button.
  const float footer_h = 84 * s;
  ui_.rect(0, H - footer_h, W, footer_h, rgb(0x0C0C0D, 0.92f));
  struct HintChip {
    const char* key;
    const char* label;
    int id;
  };
  std::vector<HintChip> chips;
  if (!games_.empty()) chips.push_back({"Enter", "Play", kHintPlay});
  chips.push_back({"O", games_.empty() ? "Choose games folder" : "Games folder", kHintFolder});
  if (!games_.empty()) chips.push_back({"I", "Import a save", kHintImport});
  chips.push_back({"N", net_.active() ? "Online game" : "Play online", kHintOnline});
  chips.push_back({"B", "Back up", kHintBackup});
  chips.push_back({"C", "Controls", kHintControls});
  chips.push_back({"F10", "Who's playing", kHintPlayers});
  chips.push_back({"F11", "Fullscreen", kHintFullscreen});
  chips.push_back({"F1", "Help", kHintHelp});
  float hx = margin - 14 * s;
  const float hy = H - footer_h + 18 * s, hh = 48 * s, hs = 19 * s;
  for (const HintChip& c : chips) {
    const float kw = ui_.measure(c.key, hs, true), lw = ui_.measure(c.label, hs);
    const float w = kw + lw + 40 * s;
    if (mouse_over(hx, hy, w, hh)) ui_.rect(hx, hy, w, hh, rgb(0xFFFFFF, 0.07f), 12 * s);
    ui_.text(hx + 14 * s, hy + 12 * s, c.key, hs, kText, true);
    ui_.text(hx + 26 * s + kw, hy + 12 * s, c.label, hs, kDim);
    add_hit(hx, hy, w, hh, kHitHint, c.id);
    hx += w + 6 * s;
  }
  draw_players_strip(W - margin - (4 * 190 + 3 * 10) * s, 70 * s, s);
  draw_backup_status(W - margin, 124 * s, s);

  if (games_.empty()) {
    const float cy = H * 0.42f;
    ui_.image(ui_.logo(), W / 2 - 64 * s, cy - 150 * s, 128 * s, 128 * s);
    ui_.text(W / 2, cy, "Add your games", 40 * s, kText, true, Align::Center);
    ui_.text(W / 2, cy + 60 * s, "Press O (or Y on a controller) to choose the folder with your N64 game files.", 22 * s,
             kDim, false, Align::Center);
    ui_.text(W / 2, cy + 94 * s, "Zipped games (.zip, .7z, .rar) work too. You can also drop a game onto this window.", 22 * s, kDim, false, Align::Center);
    const float bw = ui_.measure("Choose games folder", 22 * s, true) + 56 * s;
    draw_button(W / 2 - bw / 2, cy + 150 * s, "Choose games folder", s, true, false, kHitAddGames);
    return;
  }

  const float tile_w = 330 * s, tile_h = 200 * s, gap = 28 * s, top = 180 * s;
  const int cols = std::max(1, static_cast<int>((W - 2 * margin + gap) / (tile_w + gap)));
  const float grid_w = cols * tile_w + (cols - 1) * gap, left = (W - grid_w) / 2;
  const int rows_visible = std::max(1, static_cast<int>((H - footer_h - top + gap) / (tile_h + gap)));
  // Scroll only as far as needed to keep the selection visible, so pointing at a tile never moves
  // the grid under the mouse.
  const int sel_row = selected_ / cols, total_rows = (static_cast<int>(games_.size()) + cols - 1) / cols;
  if (sel_row < home_first_row_) home_first_row_ = sel_row;
  if (sel_row >= home_first_row_ + rows_visible) home_first_row_ = sel_row - rows_visible + 1;
  home_first_row_ = std::clamp(home_first_row_, 0, std::max(0, total_rows - rows_visible));
  const int first_row = home_first_row_;
  std::error_code ec;
  for (int i = first_row * cols; i < static_cast<int>(games_.size()); ++i) {
    const int row = i / cols - first_row, col = i % cols;
    if (row >= rows_visible) break;
    const Game& g = games_[i];
    const bool sel = i == selected_;
    const float x = left + col * (tile_w + gap), y = top + row * (tile_h + gap);
    add_hit(x, y, tile_w, tile_h, kHitTile, i);
    if (sel) {
      ui_.rect(x - 10 * s, y - 10 * s, tile_w + 20 * s, tile_h + 20 * s, kRed.with_alpha(0.10f), 26 * s);
      ui_.gradient(x, y, tile_w, tile_h, rgb(0x3A1517), kTile, 18 * s);
      ui_.outline(x, y, tile_w, tile_h, kRed, 18 * s, 3 * s);
    } else {
      ui_.rect(x, y, tile_w, tile_h, kTile, 18 * s);
      if (mouse_over(x, y, tile_w, tile_h)) ui_.rect(x, y, tile_w, tile_h, rgb(0xFFFFFF, 0.05f), 18 * s);
    }
    ui_.image(ui_.logo_small(), x + tile_w - 50 * s, y + 18 * s, 32 * s, 32 * s, sel ? 1.0f : 0.35f);
    const auto lines = wrap(ui_, g.title, 26 * s, tile_w - 90 * s, true, 3);
    for (std::size_t l = 0; l < lines.size(); ++l) {
      ui_.text(x + 22 * s, y + 22 * s + l * 32 * s, lines[l], 26 * s, sel ? kText : kText.with_alpha(0.85f), true);
    }
    std::string meta = g.region.empty() ? g.internal_name : g.region;
    ui_.text(x + 22 * s, y + tile_h - 44 * s, ui_.fit(meta, 18 * s, tile_w - 150 * s), 18 * s, kDim);
    if (fs::is_directory(library_for(g.system) / g.internal_name / "current", ec)) {
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
  const float row = 50 * s, pw = 660 * s, ph = 150 * s + items.size() * row + 70 * s;
  const float px = (W - pw) / 2, py = std::max(20 * s, (H - ph) / 2);
  ui_.rect(px, py, pw, ph, kPanel.with_alpha(0.97f), 24 * s);
  ui_.outline(px, py, pw, ph, rgb(0xFFFFFF, 0.06f), 24 * s, 1.5f * s);
  add_hit(px, py, pw, ph, kHitPanel);
  ui_.image(ui_.logo(), px + 30 * s, py + 30 * s, 48 * s, 48 * s);
  ui_.text(px + 94 * s, py + 30 * s, ui_.fit(game_ ? game_->title : "", 28 * s, pw - 130 * s, true), 28 * s, kText, true);
  const std::string state = !online_game_ ? "Paused" : net_.is_host() ? "Online: paused for everyone" : "Online: the game keeps going";
  ui_.text(px + 94 * s, py + 66 * s, state, 18 * s, kDim);
  float y = py + 120 * s;
  for (int i = 0; i < static_cast<int>(items.size()); ++i) {
    const bool sel = i == menu_index_;
    if (sel) {
      ui_.rect(px + 18 * s, y, pw - 36 * s, row - 6 * s, kRed.with_alpha(0.16f), 12 * s);
      ui_.rect(px + 18 * s, y + 10 * s, 4 * s, row - 26 * s, kRed, 2 * s);
    }
    add_hit(px + 18 * s, y, pw - 36 * s, row - 6 * s, kHitMenuRow, i);
    const bool off = items[i].value == "off online";  // brief 11.4: shown greyed out while online
    const float label_w = ui_.text(px + 40 * s, y + 11 * s, items[i].label, 21 * s, off ? kFaint : sel ? kText : kText.with_alpha(0.82f), sel);
    if (!items[i].key.empty()) {
      // The shortcut, as a small key cap after the label: it works while playing, and here too.
      const float kx = px + 40 * s + label_w + 14 * s, kw = ui_.measure(items[i].key, 15 * s, true) + 16 * s;
      ui_.rect(kx, y + 11 * s, kw, 26 * s, rgb(0xFFFFFF, sel ? 0.14f : 0.07f), 7 * s);
      ui_.text(kx + 8 * s, y + 15 * s, items[i].key, 15 * s, off ? kFaint : sel ? kText : kDim, true);
    }
    if (!items[i].value.empty()) {
      const bool adjustable = items[i].id >= kSlot && items[i].id <= kVolume;
      if (adjustable && sel) {
        // Clickable arrows either side of the value.
        const float bs = 32 * s, by = y + (row - 6 * s - bs) / 2, inc_x = px + pw - 36 * s - bs;
        const float vw = ui_.measure(items[i].value, 21 * s), dec_x = inc_x - 12 * s - vw - 12 * s - bs;
        for (const auto& [bx, glyph, kind] : {std::tuple{dec_x, "<", kHitMenuDec}, std::tuple{inc_x, ">", kHitMenuInc}}) {
          ui_.rect(bx, by, bs, bs, mouse_over(bx, by, bs, bs) ? kRed.with_alpha(0.55f) : rgb(0xFFFFFF, 0.08f), 9 * s);
          ui_.text(bx + bs / 2, by + 4 * s, glyph, 20 * s, kText, true, Align::Center);
          add_hit(bx, by, bs, bs, kind, i);
        }
        ui_.text(inc_x - 12 * s, y + 11 * s, items[i].value, 21 * s, kText, false, Align::Right);
      } else {
        ui_.text(px + pw - 40 * s, y + 11 * s, items[i].value, 21 * s, sel ? kText : kDim, false, Align::Right);
      }
    }
    y += row;
  }
  ui_.text(px + pw / 2, py + ph - 56 * s, "Up/Down or W/S choose     Left/Right change     Enter select     Esc back     Mouse: click",
           16 * s, kFaint, false, Align::Center);
  ui_.text(px + pw / 2, py + ph - 32 * s, "F12 screenshot     F1 all controls and shortcuts", 16 * s, kFaint, false,
           Align::Center);
}

void App::draw_seating() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.65f));
  ui_.text(W / 2, H * 0.16f, "Who's playing?", 48 * s, kText, true, Align::Center);
  ui_.text(W / 2, H * 0.16f + 70 * s, "To join: a controller presses any button, the keyboard presses Enter.", 22 * s, kDim,
           false, Align::Center);
  ui_.text(W / 2, H * 0.16f + 104 * s, "Two people on one keyboard: left player presses F, right player presses . (period)",
           20 * s, kFaint, false, Align::Center);
  const float cw = 380 * s, ch = 330 * s, gap = 30 * s, total = 4 * cw + 3 * gap;
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
    if (!seat.empty()) {
      const float bw = ui_.measure("Remove", 22 * s, true) + 56 * s;
      draw_button(x + w / 2 - bw / 2, y0 + ch - 76 * s, "Remove", s, false, false, kHitSeatRemove, i);
    }
  }
  if (input_.keyboard_shared()) {
    ui_.text(W / 2, y0 + ch + 40 * s, "Two players share the keyboard. Many keyboards miss keys pressed together;", 20 * s,
             rgb(0xF2C12E), false, Align::Center);
    ui_.text(W / 2, y0 + ch + 70 * s, "gamepads work better, and stick-spinning minigames are much easier on one.", 20 * s,
             rgb(0xF2C12E), false, Align::Center);
  }
  ui_.text(W / 2, H - 130 * s, "Enter, Esc or B when everyone has joined.   Delete: start over", 20 * s, kDim, false,
           Align::Center);
  draw_button(W / 2 - 8 * s, H - 92 * s, "Start over", s, false, false, kHitSeatingReset, 0, true);
  draw_button(W / 2 + 8 * s, H - 92 * s, "Done", s, true, false, kHitSeatingDone);
}

void App::draw_help() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.7f));
  const float pw = std::min(W - 40 * s, 1500 * s), ph = std::min(H - 40 * s, 820 * s);
  const float px = (W - pw) / 2, py = (H - ph) / 2;
  ui_.rect(px, py, pw, ph, kPanel, 24 * s);
  ui_.text(px + 40 * s, py + 32 * s, "Controls", 34 * s, kText, true);
  struct Row {
    std::string a, b;
  };
  // The keyboard and controller columns show the bindings in use (changed on the Controls screen).
  auto k = [&](int c) { return input_.key_label(0, c); };
  auto four = [&](int first, const char* sep) {
    return k(first) + sep + k(first + 1) + sep + k(first + 2) + sep + k(first + 3);
  };
  const bool arrows = input_.key(0, kStickUp) == SDL_SCANCODE_UP && input_.key(0, kStickDown) == SDL_SCANCODE_DOWN &&
                      input_.key(0, kStickLeft) == SDL_SCANCODE_LEFT && input_.key(0, kStickRight) == SDL_SCANCODE_RIGHT;
  const Row keyboard[] = {
      {"Control stick", arrows ? std::string("Arrow keys") : four(kStickUp, " / ")},
      {"Walk (hold)", k(kWalk)}, {"A / B", k(kA) + " / " + k(kB)}, {"Z trigger", k(kZ)},
      {"L / R", k(kL) + " / " + k(kR)}, {"Start", k(kStart)}, {"C buttons", four(kCUp, " ")},
      {"D-pad", four(kDUp, " ")},
  };
  const Row pad[] = {
      {"Control stick", "Left stick"}, {"A / B", input_.pad_label(kA) + " / " + input_.pad_label(kB)},
      {"Z trigger", input_.pad_label(kZ)}, {"L / R", input_.pad_label(kL) + " / " + input_.pad_label(kR)},
      {"Start", input_.pad_label(kStart)}, {"C-Up / C-Down", input_.pad_label(kCUp) + " / " + input_.pad_label(kCDown)},
      {"Menu", "Back or Guide"},
  };
  const Row hotkeys[] = {
      {"Esc", "Menu"}, {"F11 / Alt+Enter", "Fullscreen"}, {"F5 / F7", "Save / load state"},
      {"F8 / F9", "Volume down / up"}, {"F10", "Who's playing"}, {"F12", "Screenshot"}, {"Ctrl+R", "Reset"},
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
  column(px + 40 * s, "Keyboard", keyboard, 8);
  column(px + pw / 3 + 20 * s, "Controller", pad, 7);
  column(px + 2 * pw / 3, "Shortcuts", hotkeys, 7);
  ui_.text(px + 40 * s, py + ph - 122 * s,
           "Change any key or button on the Controls screen: press C in the library, or Menu > Controls.", 18 * s, kText);
  ui_.text(px + 40 * s, py + ph - 90 * s,
           "Mouse: click a game to pick it, double-click to play. Click menu items; right-click during a game for the menu.",
           18 * s, kDim);
  // Where saves go, with the Documents folder named as people know it.
  std::string where = utf8(library_dir_.lexically_normal());
  if (const char* docs = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS)) {
    const std::string d = utf8(from_utf8(docs).lexically_normal());
    const std::string base = d.size() > 1 && (d.back() == '/' || d.back() == '\\') ? d.substr(0, d.size() - 1) : d;
    if (where.rfind(base, 0) == 0) where = "Documents" + where.substr(base.size());
  }
  ui_.text(px + 40 * s, py + ph - 58 * s,
           ui_.fit("Your games save automatically into " + where + ", with the last 10 versions kept.", 18 * s, pw - 80 * s),
           18 * s, kDim);
}

// ---------------------------------------------------------------------------------------------
// Mouse

void App::add_hit(float x, float y, float w, float h, int kind, int arg) {
  hits_.push_back({x, y, w, h, kind, arg, drawing_overlay_});
}

const App::Hit* App::hit_at(float x, float y) const {
  const bool overlay = overlay_ != Overlay::None;
  for (auto it = hits_.rbegin(); it != hits_.rend(); ++it) {  // last drawn is on top
    if (it->overlay == overlay && x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h) return &*it;
  }
  return nullptr;
}

bool App::mouse_recent() const { return mouse_seen_ns_ && SDL_GetTicksNS() - mouse_seen_ns_ < kMouseIdleNs; }

bool App::mouse_over(float x, float y, float w, float h) const {
  return (screen_ == Screen::Home || overlay_ != Overlay::None || mouse_recent()) && mouse_x_ >= x && mouse_y_ >= y &&
         mouse_x_ < x + w && mouse_y_ < y + h;
}

void App::handle_mouse(const SDL_Event& e) {
  const float d = SDL_GetWindowPixelDensity(window_) > 0 ? SDL_GetWindowPixelDensity(window_) : 1.0f;
  if (e.type == SDL_EVENT_MOUSE_MOTION) {
    mouse_x_ = e.motion.x * d;
    mouse_y_ = e.motion.y * d;
    mouse_seen_ns_ = SDL_GetTicksNS();
    if (const Hit* h = hit_at(mouse_x_, mouse_y_)) hover(*h);
    return;
  }
  if (e.type == SDL_EVENT_MOUSE_WHEEL) {
    const int dir = e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0;
    if (dir == 0) return;
    if (overlay_ == Overlay::Menu) {
      const Hit* h = hit_at(mouse_x_, mouse_y_);
      const auto items = menu_items();
      if (h && (h->kind == kHitMenuRow || h->kind == kHitMenuDec || h->kind == kHitMenuInc) &&
          items[h->arg].id >= kSlot && items[h->arg].id <= kVolume) {
        menu_index_ = h->arg;
        ui_action(dir > 0 ? kRight : kLeft);
      }
    } else if (overlay_ == Overlay::None && screen_ == Screen::Home) {
      ui_action(dir > 0 ? kUp : kDown);
    }
    return;
  }
  // Button press.
  mouse_x_ = e.button.x * d;
  mouse_y_ = e.button.y * d;
  mouse_seen_ns_ = SDL_GetTicksNS();
  if (e.button.button == SDL_BUTTON_RIGHT) {
    if (overlay_ != Overlay::None) {
      ui_action(kBack);
    } else if (screen_ == Screen::Playing) {
      ui_action(kMenu);
    }
    return;
  }
  if (e.button.button != SDL_BUTTON_LEFT) return;
  if (const Hit* h = hit_at(mouse_x_, mouse_y_)) {
    const Hit hit = *h;  // a copy: hits_ is rebuilt whenever the screen is drawn again
    click(hit, e.button.clicks);
  } else if (overlay_ != Overlay::None) {
    ui_action(kBack);  // a click outside an open menu closes it
  }
}

void App::hover(const Hit& h) {
  switch (h.kind) {
    case kHitMenuRow: case kHitMenuDec: case kHitMenuInc: menu_index_ = h.arg; break;
    case kHitEntry: if (!controls_capture_) controls_index_ = h.arg; break;
    default: break;
  }
}

void App::click(const Hit& h, int clicks) {
  switch (h.kind) {
    case kHitTile:
      // One click picks the game (the one Enter plays and Play online hosts), a double click plays it.
      selected_ = h.arg;
      if (clicks >= 2) start_game(games_[h.arg]);
      break;
    case kHitAddGames: choose_games_folder(); break;
    case kHitHint:
      switch (h.arg) {
        case kHintPlay: ui_action(kAccept); break;
        case kHintFolder: choose_games_folder(); break;
        case kHintImport: choose_save_to_import(); break;
        case kHintControls: open_controls(); break;
        case kHintOnline: open_online(); break;
        case kHintBackup: open_backup(); break;
        case kHintPlayers:
          input_.start_seating();
          overlay_ = Overlay::Seating;
          break;
        case kHintFullscreen: set_fullscreen(!fullscreen_); break;
        case kHintHelp: overlay_ = Overlay::Help; break;
        default: break;
      }
      break;
    case kHitMenuRow: menu_index_ = h.arg; ui_action(kAccept); break;
    case kHitMenuDec: menu_index_ = h.arg; ui_action(kLeft); break;
    case kHitMenuInc: menu_index_ = h.arg; ui_action(kRight); break;
    case kHitMenuButton: ui_action(kMenu); break;
    case kHitTab:
      controls_tab_ = h.arg;
      controls_index_ = 0;
      controls_capture_ = false;
      break;
    case kHitEntry: controls_index_ = h.arg; ui_action(kAccept); break;
    case kHitSeatingDone: ui_action(kAccept); break;
    case kHitSeatRemove: input_.remove_seat(h.arg); break;
    case kHitSeatingReset: input_.clear_seats(); break;
    case kHitOnline: online_click(h.arg); break;
    case kHitBackup: backup_click(h.arg); break;
    case kHitField: focus_field(static_cast<TextField>(h.arg)); break;
    default:
      if (overlay_ == Overlay::Online) focus_field(TextField::None);  // a click elsewhere leaves the text field
      break;  // kHitPanel: a click on a menu's background does nothing
  }
}

// ---------------------------------------------------------------------------------------------
// Controls screen

std::vector<App::ControlsEntry> App::controls_entries() const {
  std::vector<ControlsEntry> out;
  int row = 0;
  if (controls_tab_ < Input::kKeyLayouts) {
    for (int c : std::initializer_list<int>{kStickUp, kStickDown, kStickLeft, kStickRight, kWalk, kDUp, kDDown, kDLeft,
                                            kDRight}) {
      out.push_back({c, 0, row++});
    }
  } else {
    row = 1;  // row 0 shows the control stick, which is always the left stick
    for (int c : {kDUp, kDDown, kDLeft, kDRight}) out.push_back({c, 0, row++});
  }
  row = 0;
  for (int c : {kA, kB, kZ, kStart, kL, kR, kCUp, kCDown, kCLeft, kCRight}) out.push_back({c, 1, row++});
  out.push_back({kEntryReset, 0, 100});
  out.push_back({kEntryDone, 1, 100});
  return out;
}

void App::open_controls() {
  if (overlay_ == Overlay::Controls) return;
  controls_return_ = overlay_ == Overlay::Menu ? Overlay::Menu : Overlay::None;
  overlay_ = Overlay::Controls;
  controls_index_ = 0;
  controls_capture_ = false;
  // Start on the tab for the device player 1 uses.
  const Seat& p1 = input_.seat(0);
  controls_tab_ = p1.kind == DeviceKind::Gamepad ? Input::kKeyLayouts : p1.kind == DeviceKind::Keyboard ? p1.layout : 0;
}

void App::close_controls() {
  controls_capture_ = false;
  input_.save_bindings(settings_);
  settings_.save();
  overlay_ = controls_return_;
}

void App::controls_action(int action) {
  const auto entries = controls_entries();
  const int n = static_cast<int>(entries.size());
  controls_index_ = std::clamp(controls_index_, 0, n - 1);
  const ControlsEntry cur = entries[controls_index_];
  switch (action) {
    case kUp: controls_index_ = (controls_index_ + n - 1) % n; break;
    case kDown: controls_index_ = (controls_index_ + 1) % n; break;
    case kLeft: case kRight: {
      // Jump to the nearest row in the other column.
      int best = controls_index_, best_d = 1 << 30;
      for (int i = 0; i < n; ++i) {
        if (entries[i].column == cur.column) continue;
        const int dist = std::abs(entries[i].row - cur.row);
        if (dist < best_d) {
          best = i;
          best_d = dist;
        }
      }
      controls_index_ = best;
      break;
    }
    case kPrevTab: case kNextTab:
      controls_tab_ = (controls_tab_ + (action == kNextTab ? 1 : Input::kKeyLayouts)) % (Input::kKeyLayouts + 1);
      controls_index_ = 0;
      break;
    case kAccept:
      if (cur.control == kEntryDone) {
        close_controls();
      } else if (cur.control == kEntryReset) {
        if (controls_tab_ < Input::kKeyLayouts) {
          input_.reset_keys(controls_tab_);
        } else {
          input_.reset_pad();
        }
        input_.save_bindings(settings_);
        settings_.save();
        toast("Controls reset to the defaults");
      } else {
        controls_capture_ = true;
      }
      break;
    case kBack: case kMenu: close_controls(); break;
    default: break;
  }
}

bool App::handle_capture(const SDL_Event& e) {
  const auto entries = controls_entries();
  const int control = entries[std::clamp(controls_index_, 0, static_cast<int>(entries.size()) - 1)].control;
  const bool keyboard_tab = controls_tab_ < Input::kKeyLayouts;
  auto finish = [&]() {
    controls_capture_ = false;
    input_.save_bindings(settings_);
    settings_.save();
  };
  switch (e.type) {
    case SDL_EVENT_KEY_DOWN: {
      if (e.key.repeat) return true;
      const SDL_Scancode sc = e.key.scancode;
      if (sc == SDL_SCANCODE_ESCAPE) {
        controls_capture_ = false;
      } else if (!keyboard_tab) {
        toast("Press a button on the controller (Esc cancels)");
      } else if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) {
        toast("The F keys are shortcuts. Choose another key (Esc cancels)");
      } else {
        input_.bind_key(controls_tab_, control, sc);
        finish();
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
      const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
      if (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_GUIDE) {
        controls_capture_ = false;  // these open the menu in a game, so they stay free
      } else if (keyboard_tab) {
        toast("Press a key on the keyboard (Back on the controller cancels)");
      } else if (input_.bind_pad(control, e)) {
        finish();
      }
      return true;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (!keyboard_tab && input_.bind_pad(control, e)) finish();
      return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      controls_capture_ = false;  // clicking anywhere cancels
      return true;
    default:
      return false;
  }
}

float App::draw_button(float x, float y, const std::string& label, float s, bool primary, bool focused, int kind,
                       int arg, bool right_aligned, bool enabled) {
  const float fs = 22 * s, w = ui_.measure(label, fs, true) + 56 * s, h = 52 * s;
  if (right_aligned) x -= w;
  const bool hot = enabled && mouse_over(x, y, w, h);
  Color fill = primary ? kRed : rgb(0xFFFFFF, 0.08f);
  if (hot) fill = primary ? rgb(0xF2474E) : rgb(0xFFFFFF, 0.14f);
  if (!enabled) fill = primary ? kRed.with_alpha(0.3f) : rgb(0xFFFFFF, 0.04f);
  ui_.rect(x, y, w, h, fill, 14 * s);
  if (focused) ui_.outline(x - 4 * s, y - 4 * s, w + 8 * s, h + 8 * s, kText.with_alpha(0.8f), 17 * s, 2 * s);
  ui_.text(x + w / 2, y + 13 * s, label, fs, enabled ? kText : kText.with_alpha(0.4f), true, Align::Center);
  if (enabled) add_hit(x, y, w, h, kind, arg);
  return w;
}

void App::draw_controls() {
  const float s = out_h_ / 1080.0f, W = static_cast<float>(out_w_), H = static_cast<float>(out_h_);
  ui_.rect(0, 0, W, H, rgb(0x000000, 0.7f));
  const float pw = std::min(W - 40 * s, 1180 * s), ph = std::min(H - 40 * s, 900 * s);
  const float px = (W - pw) / 2, py = (H - ph) / 2;
  ui_.rect(px, py, pw, ph, kPanel, 24 * s);
  ui_.outline(px, py, pw, ph, rgb(0xFFFFFF, 0.06f), 24 * s, 1.5f * s);
  add_hit(px, py, pw, ph, kHitPanel);
  ui_.text(px + 40 * s, py + 30 * s, "Controls", 34 * s, kText, true);
  ui_.text(px + 40 * s, py + 78 * s, "Click a control (or select it and press Enter), then press the key or button you want for it.",
           18 * s, kDim);

  // Tabs.
  const char* tabs[] = {"Keyboard", "Left half", "Right half", "Controller"};
  float tx = px + 40 * s;
  const float ty = py + 118 * s, th = 46 * s;
  for (int t = 0; t < 4; ++t) {
    const float w = ui_.measure(tabs[t], 20 * s, true) + 44 * s;
    const bool on = t == controls_tab_;
    ui_.rect(tx, ty, w, th, on ? kRed : mouse_over(tx, ty, w, th) ? rgb(0xFFFFFF, 0.14f) : kTile, 23 * s);
    ui_.text(tx + w / 2, ty + 12 * s, tabs[t], 20 * s, on ? kText : kDim, true, Align::Center);
    add_hit(tx, ty, w, th, kHitTab, t);
    tx += w + 10 * s;
  }
  ui_.text(tx + 10 * s, ty + 14 * s, "Q / E switch", 16 * s, kFaint);
  const char* notes[] = {
      "One player using the whole keyboard.",
      "The left player when two people share the keyboard (Who's playing, then F).",
      "The right player when two people share the keyboard (Who's playing, then . ).",
      "Every controller. The left stick is always the control stick.",
  };
  ui_.text(px + 40 * s, ty + th + 18 * s, notes[controls_tab_], 18 * s, kFaint);

  // Two columns of controls.
  const auto entries = controls_entries();
  const float top = ty + th + 66 * s, row_h = 46 * s, colw = (pw - 120 * s) / 2;
  const float colx[2] = {px + 40 * s, px + 80 * s + colw};
  ui_.text(colx[0] + 14 * s, top, "Movement", 20 * s, kRed, true);
  ui_.text(colx[1] + 14 * s, top, "Buttons", 20 * s, kRed, true);
  const float rows_top = top + 40 * s;
  const bool pulse = (SDL_GetTicks() / 450) % 2 == 0;
  auto chip = [&](float right, float y, const std::string& text, bool waiting, bool fixed) {
    const float fs = 19 * s, cw = colw * 0.46f, x = right - cw;
    ui_.rect(x, y + 5 * s, cw, row_h - 14 * s, waiting ? kRed.with_alpha(pulse ? 0.45f : 0.25f) : rgb(0xFFFFFF, fixed ? 0.04f : 0.09f), 10 * s);
    ui_.text(x + cw / 2, y + 11 * s, ui_.fit(text, fs, cw - 20 * s), fs, fixed ? kFaint : kText, false, Align::Center);
  };
  if (controls_tab_ == Input::kKeyLayouts) {
    ui_.text(colx[0] + 14 * s, rows_top + 11 * s, "Control stick", 20 * s, kDim);
    chip(colx[0] + colw - 10 * s, rows_top, "Left stick", false, true);
  }
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    const ControlsEntry& en = entries[i];
    if (en.control < 0) continue;
    const float x = colx[en.column], y = rows_top + en.row * row_h;
    const bool focus = i == controls_index_, waiting = focus && controls_capture_;
    if (focus) ui_.rect(x, y, colw, row_h - 4 * s, kRed.with_alpha(0.16f), 12 * s);
    else if (mouse_over(x, y, colw, row_h - 4 * s)) ui_.rect(x, y, colw, row_h - 4 * s, rgb(0xFFFFFF, 0.05f), 12 * s);
    ui_.text(x + 14 * s, y + 11 * s, Input::control_name(en.control), 20 * s, focus ? kText : kText.with_alpha(0.85f), focus);
    std::string value;
    if (waiting) {
      value = controls_tab_ < Input::kKeyLayouts ? "Press a key..." : "Press a button...";
    } else if (controls_tab_ < Input::kKeyLayouts) {
      value = input_.key_label(controls_tab_, en.control);
    } else {
      value = input_.pad_label(en.control);
    }
    chip(x + colw - 10 * s, y, value, waiting, false);
    add_hit(x, y, colw, row_h - 4 * s, kHitEntry, i);
  }

  // Bottom buttons.
  const float by = py + ph - 84 * s;
  int reset_i = 0, done_i = 0;
  for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
    if (entries[i].control == kEntryReset) reset_i = i;
    if (entries[i].control == kEntryDone) done_i = i;
  }
  draw_button(px + 40 * s, by, "Reset to defaults", s, false, controls_index_ == reset_i, kHitEntry, reset_i);
  draw_button(px + pw - 40 * s, by, "Done", s, true, controls_index_ == done_i, kHitEntry, done_i, true);
  ui_.text(px + pw / 2, by + 16 * s,
           controls_capture_ ? "Esc cancels" : "A key that is already in use swaps places with this one",
           16 * s, kFaint, false, Align::Center);
}

void App::draw_toasts() {
  const uint64_t now = SDL_GetTicksNS();
  while (!toasts_.empty() && toasts_.front().until_ns < now) toasts_.pop_front();
  const float s = out_h_ / 1080.0f;
  // Above the library's footer, or at the top over the online screen (its buttons are at the bottom).
  const bool top = overlay_ == Overlay::Online || overlay_ == Overlay::Backup;
  float y = top ? 12 * s : out_h_ - (screen_ == Screen::Home ? 110 : 40) * s;
  for (auto it = toasts_.rbegin(); it != toasts_.rend(); ++it) {
    const float left_ms = static_cast<float>(it->until_ns - now) / 1e6f;
    const float alpha = std::min(1.0f, left_ms / 400.0f);
    const std::string t = ui_.fit(it->text, 20 * s, out_w_ - 120 * s);
    const float w = ui_.measure(t, 20 * s) + 48 * s, h = 48 * s;
    if (top) {
      if (it != toasts_.rbegin()) y += h + 10 * s;
    } else {
      y -= h + 10 * s;
    }
    ui_.rect((out_w_ - w) / 2, y, w, h, rgb(0x121214, 0.92f * alpha), 14 * s);
    ui_.rect((out_w_ - w) / 2 + 14 * s, y + h / 2 - 4 * s, 8 * s, 8 * s, kRed.with_alpha(alpha), 4 * s);
    ui_.text((out_w_ - w) / 2 + 32 * s, y + 13 * s, t, 20 * s, kText.with_alpha(alpha));
  }
}

}  // namespace p64
