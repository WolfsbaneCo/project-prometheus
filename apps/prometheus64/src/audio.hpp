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
  void set_speed(double ratio);  // how much faster the game runs than its own clock (screen-locked pacing)
  // Fast-forward: the game makes this many times its sound each second, and it plays that much
  // faster (higher, as fast-forward sounds), so it keeps up instead of piling up. 1: normal.
  void set_fast(double times);
  double queued_seconds() const;

 private:
  SDL_AudioStream* stream_ = nullptr;
  double rate_ = 0;
  double speed_ = 1.0;
  double fast_ = 1.0;
  double target_ = 0.05;  // seconds of sound to keep queued
  bool primed_ = false;  // the cushion of sound is in place
};

}  // namespace p64
