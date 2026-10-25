#include "core.hpp"

#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_log.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>

namespace p64 {
namespace {

std::string utf8(const std::filesystem::path& p) {
  const auto u = p.u8string();
  return std::string(u.begin(), u.end());
}

Core* g_core = nullptr;  // libretro callbacks carry no user pointer

void RETRO_CALLCONV video_cb(const void* data, unsigned w, unsigned h, std::size_t pitch) {
  if (g_core && g_core->hooks.video) g_core->hooks.video(data, w, h, pitch);
}

void RETRO_CALLCONV audio_cb(int16_t left, int16_t right) {
  const int16_t frame[2] = {left, right};
  if (g_core && g_core->hooks.audio) g_core->hooks.audio(frame, 1);
}

std::size_t RETRO_CALLCONV audio_batch_cb(const int16_t* data, std::size_t frames) {
  static const bool debug = std::getenv("P64_DEBUG") != nullptr;
  static std::size_t calls = 0;
  if (debug && calls++ < 3) SDL_Log("[core] audio batch: %llu frames", static_cast<unsigned long long>(frames));
  if (g_core && g_core->hooks.audio) g_core->hooks.audio(data, frames);
  return frames;
}

void RETRO_CALLCONV input_poll_cb() {
  if (g_core && g_core->hooks.input_poll) g_core->hooks.input_poll();
}

int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
  return g_core && g_core->hooks.input_state ? g_core->hooks.input_state(port, device, index, id) : 0;
}

bool RETRO_CALLCONV rumble_cb(unsigned port, retro_rumble_effect effect, uint16_t strength) {
  return g_core && g_core->hooks.rumble && g_core->hooks.rumble(port, effect, strength);
}

uintptr_t RETRO_CALLCONV framebuffer_cb() {
  return g_core && g_core->hooks.framebuffer ? g_core->hooks.framebuffer() : 0;
}

retro_proc_address_t RETRO_CALLCONV proc_address_cb(const char* name) {
  return g_core && g_core->hooks.proc_address ? g_core->hooks.proc_address(name) : nullptr;
}

void RETRO_CALLCONV log_cb(retro_log_level level, const char* fmt, ...) {
  static const bool verbose = std::getenv("P64_CORE_LOG") != nullptr;
  if (level < RETRO_LOG_WARN && !verbose) return;
  char buf[2048];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, args);
  va_end(args);
  std::size_t n = std::strlen(buf);
  while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
  SDL_Log("[core] %s", buf);
}

}  // namespace

struct Core::Api {
  void(RETRO_CALLCONV* set_environment)(retro_environment_t);
  void(RETRO_CALLCONV* set_video_refresh)(retro_video_refresh_t);
  void(RETRO_CALLCONV* set_audio_sample)(retro_audio_sample_t);
  void(RETRO_CALLCONV* set_audio_sample_batch)(retro_audio_sample_batch_t);
  void(RETRO_CALLCONV* set_input_poll)(retro_input_poll_t);
  void(RETRO_CALLCONV* set_input_state)(retro_input_state_t);
  void(RETRO_CALLCONV* init)();
  void(RETRO_CALLCONV* deinit)();
  unsigned(RETRO_CALLCONV* api_version)();
  void(RETRO_CALLCONV* get_system_info)(retro_system_info*);
  void(RETRO_CALLCONV* get_system_av_info)(retro_system_av_info*);
  void(RETRO_CALLCONV* set_controller_port_device)(unsigned, unsigned);
  void(RETRO_CALLCONV* reset)();
  void(RETRO_CALLCONV* run)();
  std::size_t(RETRO_CALLCONV* serialize_size)();
  bool(RETRO_CALLCONV* serialize)(void*, std::size_t);
  bool(RETRO_CALLCONV* unserialize)(const void*, std::size_t);
  bool(RETRO_CALLCONV* load_game)(const retro_game_info*);
  void(RETRO_CALLCONV* unload_game)();
  void*(RETRO_CALLCONV* get_memory_data)(unsigned);
  std::size_t(RETRO_CALLCONV* get_memory_size)(unsigned);
};

Core::Core() = default;

Core::~Core() { close(); }

void Core::close() {
  if (game_loaded_) unload_game();
  if (initialised_ && api_) api_->deinit();
  initialised_ = false;
  delete api_;
  api_ = nullptr;
  if (library_) SDL_UnloadObject(static_cast<SDL_SharedObject*>(library_));
  library_ = nullptr;
  if (g_core == this) g_core = nullptr;
}

std::string Core::open(const std::filesystem::path& library) {
  defaults_.clear();
  current_ = overrides_;
  options_changed_ = false;
  hw_requested_ = false;
  pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555;
  library_path_str_ = utf8(library);
  auto* so = SDL_LoadObject(library_path_str_.c_str());
  if (!so) return std::string("could not load the emulator core: ") + SDL_GetError();
  library_ = so;
  api_ = new Api{};
  bool missing = false;
  auto load = [&](auto& fn, const char* name) {
    fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(SDL_LoadFunction(so, name));
    if (!fn) missing = true;
  };
  load(api_->set_environment, "retro_set_environment");
  load(api_->set_video_refresh, "retro_set_video_refresh");
  load(api_->set_audio_sample, "retro_set_audio_sample");
  load(api_->set_audio_sample_batch, "retro_set_audio_sample_batch");
  load(api_->set_input_poll, "retro_set_input_poll");
  load(api_->set_input_state, "retro_set_input_state");
  load(api_->init, "retro_init");
  load(api_->deinit, "retro_deinit");
  load(api_->api_version, "retro_api_version");
  load(api_->get_system_info, "retro_get_system_info");
  load(api_->get_system_av_info, "retro_get_system_av_info");
  load(api_->set_controller_port_device, "retro_set_controller_port_device");
  load(api_->reset, "retro_reset");
  load(api_->run, "retro_run");
  load(api_->serialize_size, "retro_serialize_size");
  load(api_->serialize, "retro_serialize");
  load(api_->unserialize, "retro_unserialize");
  load(api_->load_game, "retro_load_game");
  load(api_->unload_game, "retro_unload_game");
  load(api_->get_memory_data, "retro_get_memory_data");
  load(api_->get_memory_size, "retro_get_memory_size");
  if (missing) return "the emulator core is incomplete or not a libretro core";
  if (api_->api_version() != RETRO_API_VERSION) return "the emulator core uses an unsupported libretro version";

  g_core = this;
  system_dir_str_ = utf8(system_dir);
  save_dir_str_ = utf8(save_dir);
  api_->set_environment(env_trampoline);
  api_->init();
  initialised_ = true;
  api_->set_video_refresh(video_cb);
  api_->set_audio_sample(audio_cb);
  api_->set_audio_sample_batch(audio_batch_cb);
  api_->set_input_poll(input_poll_cb);
  api_->set_input_state(input_state_cb);
  api_->get_system_info(&info_);
  return {};
}

std::string Core::load_game(const std::filesystem::path& rom) {
  if (!api_) return "no emulator core loaded";
  if (game_loaded_) unload_game();
  rom_path_ = utf8(rom);
  std::ifstream in(rom, std::ios::binary);
  if (!in) return "could not open the game file";
  rom_data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());

  retro_game_info game{};
  game.path = rom_path_.c_str();
  if (!info_.need_fullpath) {
    game.data = rom_data_.data();
    game.size = rom_data_.size();
  }
  hw_requested_ = false;
  if (!api_->load_game(&game)) return "the emulator core could not start this game";
  game_loaded_ = true;
  api_->get_system_av_info(&av_);
  for (unsigned port = 0; port < 4; ++port) api_->set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
  return {};
}

void Core::unload_game() {
  if (!game_loaded_) return;
  api_->unload_game();
  game_loaded_ = false;
  hw_requested_ = false;
}

void Core::context_reset() {
  if (hw_requested_ && hw_.context_reset) hw_.context_reset();
}

void Core::context_destroy() {
  if (hw_requested_ && hw_.context_destroy) hw_.context_destroy();
}

void Core::run() {
  if (game_loaded_) api_->run();
}

void Core::reset() {
  if (game_loaded_) api_->reset();
}

std::vector<uint8_t> Core::save_state() {
  std::vector<uint8_t> out;
  if (!game_loaded_) return out;
  out.resize(api_->serialize_size());
  if (out.empty() || !api_->serialize(out.data(), out.size())) out.clear();
  return out;
}

bool Core::load_state(const std::vector<uint8_t>& state) {
  return game_loaded_ && !state.empty() && api_->unserialize(state.data(), state.size());
}

uint8_t* Core::save_ram() {
  return game_loaded_ ? static_cast<uint8_t*>(api_->get_memory_data(RETRO_MEMORY_SAVE_RAM)) : nullptr;
}

std::size_t Core::save_ram_size() {
  return game_loaded_ ? api_->get_memory_size(RETRO_MEMORY_SAVE_RAM) : 0;
}

void Core::set_option(const std::string& key, const std::string& value) {
  overrides_[key] = value;
  current_[key] = value;
  options_changed_ = true;
}

std::string Core::option(const std::string& key) const {
  auto it = current_.find(key);
  return it == current_.end() ? std::string() : it->second;
}

bool RETRO_CALLCONV Core::env_trampoline(unsigned cmd, void* data) {
  return g_core && g_core->environment(cmd, data);
}

bool Core::environment(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
      *static_cast<bool*>(data) = true;
      return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE: {
      auto* m = static_cast<const retro_message*>(data);
      if (hooks.message && m->msg) hooks.message(m->msg);
      return true;
    }
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
      return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
      *static_cast<const char**>(data) = system_dir_str_.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
      *static_cast<const char**>(data) = save_dir_str_.c_str();
      return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
      *static_cast<const char**>(data) = library_path_str_.c_str();
      return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
      auto f = *static_cast<const retro_pixel_format*>(data);
      if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_0RGB1555) return false;
      pixel_format_ = f;
      return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: {
      auto* cb = static_cast<retro_hw_render_callback*>(data);
      if (cb->context_type != RETRO_HW_CONTEXT_OPENGL_CORE && cb->context_type != RETRO_HW_CONTEXT_OPENGL) return false;
      cb->get_current_framebuffer = framebuffer_cb;
      cb->get_proc_address = proc_address_cb;
      hw_ = *cb;
      hw_requested_ = true;
      return true;
    }
    case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
      *static_cast<unsigned*>(data) = RETRO_HW_CONTEXT_OPENGL_CORE;
      return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
      *static_cast<unsigned*>(data) = 0;  // the core then sends the plain SET_VARIABLES list
      return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES: {
      // Each entry is "Description; first|second|third"; the first value is the default.
      for (auto* v = static_cast<const retro_variable*>(data); v && v->key; ++v) {
        std::string spec = v->value ? v->value : "";
        auto semi = spec.find("; ");
        std::string values = semi == std::string::npos ? spec : spec.substr(semi + 2);
        defaults_[v->key] = values.substr(0, values.find('|'));
        auto o = overrides_.find(v->key);
        current_[v->key] = o != overrides_.end() ? o->second : defaults_[v->key];
      }
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
      auto* v = static_cast<retro_variable*>(data);
      auto it = current_.find(v->key);
      if (it == current_.end()) {
        v->value = nullptr;
        return false;
      }
      v->value = it->second.c_str();
      return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
      *static_cast<bool*>(data) = options_changed_;
      options_changed_ = false;
      return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE:
      static_cast<retro_rumble_interface*>(data)->set_rumble_state = rumble_cb;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
      *static_cast<uint64_t*>(data) = (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_ANALOG);
      return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
      static_cast<retro_log_callback*>(data)->log = log_cb;
      return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
      av_ = *static_cast<const retro_system_av_info*>(data);
      if (hooks.geometry_changed) hooks.geometry_changed();
      return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
      av_.geometry = *static_cast<const retro_game_geometry*>(data);
      if (hooks.geometry_changed) hooks.geometry_changed();
      return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
      *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
      return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
      return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE:
      *static_cast<int*>(data) = 3;  // video and audio both wanted
      return true;
    case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
      return true;
    default:
      return false;
  }
}

}  // namespace p64
