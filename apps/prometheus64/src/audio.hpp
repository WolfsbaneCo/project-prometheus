#pragma once

#include <SDL3/SDL_audio.h>

#include <cstddef>
#include <cstdint>

namespace p64 {

// Plays the core's 16-bit stereo samples. SDL resamples to the device rate.
class Audio {
 public:
  ~Audio() { close(); }
  bool open(double sample_rate);
  void close();
  void push(const int16_t* stereo, std::size_t frames);
  void clear();
  void set_volume(float volume);  // 0..1
  void set_paused(bool paused);
  double queued_seconds() const;

 private:
  SDL_AudioStream* stream_ = nullptr;
  double rate_ = 0;
};

}  // namespace p64
