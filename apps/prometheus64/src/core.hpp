// Loads a libretro emulator core (Mupen64Plus-Next) at runtime and answers its requests.
// Everything the core needs from the outside world (video, audio, input, options, folders)
// goes through the hooks below, so the rest of the app never touches the libretro API.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "libretro.h"

namespace p64 {

class Core {
 public:
  struct Hooks {
    // Software frame (data != nullptr) or hardware frame (data == RETRO_HW_FRAME_BUFFER_VALID).
    std::function<void(const void* data, unsigned width, unsigned height, std::size_t pitch)> video;
    std::function<void(const int16_t* stereo, std::size_t frames)> audio;
    std::function<void()> input_poll;
    std::function<int16_t(unsigned port, unsigned device, unsigned index, unsigned id)> input_state;
    std::function<bool(unsigned port, retro_rumble_effect effect, uint16_t strength)> rumble;
    std::function<void(const std::string& text)> message;
    std::function<uintptr_t()> framebuffer;  // FBO the core renders into
    std::function<retro_proc_address_t(const char* name)> proc_address;
    std::function<void()> geometry_changed;
  };

  Core();
  ~Core();
  Core(const Core&) = delete;
  Core& operator=(const Core&) = delete;

  Hooks hooks;
  std::filesystem::path system_dir;
  std::filesystem::path save_dir;
  // Where games from archives are unpacked for cores that only read games from a file (gpSP).
  std::filesystem::path unpack_dir;

  // Loads the core library and calls retro_init. Returns an error message, or empty on success.
  std::string open(const std::filesystem::path& library);
  // `rom` is the name the core sees; `data` is the whole game (the app unpacks archives itself).
  // `on_disk`: `rom` is the game's own file. When it is not (the game was in an archive) and the
  // core reads games only from a file, `data` is written to a file in unpack_dir for it.
  std::string load_game(const std::filesystem::path& rom, std::vector<uint8_t> data, bool on_disk);
  void unload_game();
  // Unloads the game, shuts the core down and releases the library. Call while the GL context
  // still exists: the core's renderer frees GPU objects on the way out.
  void close();
  bool game_loaded() const { return game_loaded_; }

  void run();
  void reset();

  std::vector<uint8_t> save_state();
  bool load_state(const std::vector<uint8_t>& state);

  // RETRO_MEMORY_SAVE_RAM: the cartridge and Controller Pak memory, in the core's layout.
  uint8_t* save_ram();
  std::size_t save_ram_size();
  // RETRO_MEMORY_SYSTEM_RAM: the console's RDRAM. Online play compares it to detect desyncs.
  const uint8_t* system_ram();
  std::size_t system_ram_size();

  // Options take effect at the next game load unless the core re-reads them live.
  void set_option(const std::string& key, const std::string& value);
  std::string option(const std::string& key) const;

  const retro_system_av_info& av() const { return av_; }
  const retro_system_info& info() const { return info_; }
  bool hardware_rendering() const { return hw_requested_; }
  const retro_hw_render_callback& hw() const { return hw_; }
  retro_pixel_format pixel_format() const { return pixel_format_; }
  // The core's link-play interface (GBA: gpSP), or nullptr when it has none.
  const retro_netpacket_callback* netpacket() const { return netpacket_.start ? &netpacket_ : nullptr; }

  // Called by the app once its GL context exists (after load_game), and before it is destroyed.
  void context_reset();
  void context_destroy();

 private:
  void remove_unpacked();
  struct Api;
  bool environment(unsigned cmd, void* data);
  static bool RETRO_CALLCONV env_trampoline(unsigned cmd, void* data);

  void* library_ = nullptr;
  Api* api_ = nullptr;
  bool initialised_ = false;
  bool game_loaded_ = false;
  retro_system_info info_{};
  retro_system_av_info av_{};
  retro_pixel_format pixel_format_ = RETRO_PIXEL_FORMAT_0RGB1555;
  bool hw_requested_ = false;
  retro_hw_render_callback hw_{};
  retro_netpacket_callback netpacket_{};
  std::map<std::string, std::string> defaults_;   // from the core's option list
  std::map<std::string, std::string> overrides_;  // set by the app
  std::map<std::string, std::string> current_;    // what GET_VARIABLE hands out (stable storage)
  bool options_changed_ = false;
  std::string system_dir_str_, save_dir_str_, library_path_str_;
  std::vector<uint8_t> rom_data_;
  std::string rom_path_;
  std::filesystem::path unpacked_;  // the file written for the core, removed with the game
  unsigned unpack_count_ = 0;
};

}  // namespace p64
