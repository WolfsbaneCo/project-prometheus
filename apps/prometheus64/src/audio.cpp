#include "audio.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <vector>

namespace p64 {

namespace {
constexpr double kTargetQueue = 0.05;  // seconds of sound waiting to play: low lag, no gaps (at least)
constexpr double kMaxQueue = 0.25;     // beyond this (after a long hitch) drop sound rather than lag
constexpr double kMaxAdjust = 0.005;   // speed changes up to 0.5%: far too small to hear
}  // namespace

bool Audio::open(double sample_rate) {
  close();
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(sample_rate + 0.5)};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream_) return false;
  rate_ = sample_rate;
  primed_ = false;
  // The sound card takes its sound in chunks: keep at least two of them queued.
  SDL_AudioSpec device{};
  int chunk = 0;
  target_ = kTargetQueue;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream_), &device, &chunk) && chunk > 0 && device.freq > 0) {
    target_ = std::max(kTargetQueue, 2.0 * chunk / device.freq);
  }
  SDL_Log("audio: %d Hz, sound card chunk %d frames, keeping %.3fs queued", device.freq, chunk, target_);
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void Audio::close() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  stream_ = nullptr;
}

void Audio::push(const int16_t* stereo, std::size_t frames) {
  if (!stream_) return;
  // The game and the sound card never run at exactly the same speed. Instead of dropping or
  // missing sound now and then (clicks), play it a touch faster or slower so the queue stays
  // near its target (dynamic rate control).
  if (!primed_) {
    // Start with the target amount of silence queued, so the first frames' sound has a cushion
    // instead of the queue building up slowly (with gaps) from empty.
    const std::vector<int16_t> silence(static_cast<std::size_t>(target_ * rate_) * 2, 0);
    SDL_PutAudioStreamData(stream_, silence.data(), static_cast<int>(silence.size() * sizeof(int16_t)));
    primed_ = true;
  }
  const double queued = queued_seconds();
  if (queued > std::max(kMaxQueue, 3 * target_)) return;
  const double off = std::clamp((queued - target_) / target_, -1.0, 1.0);
  SDL_SetAudioStreamFrequencyRatio(stream_, static_cast<float>(speed_ * (1.0 + kMaxAdjust * off)));
  SDL_PutAudioStreamData(stream_, stereo, static_cast<int>(frames * 4));
}

void Audio::set_speed(double ratio) { speed_ = std::clamp(ratio, 0.95, 1.05); }

void Audio::clear() {
  if (stream_) SDL_ClearAudioStream(stream_);
  primed_ = false;
}

void Audio::set_volume(float volume) {
  if (stream_) SDL_SetAudioStreamGain(stream_, volume);
}

void Audio::set_paused(bool paused) {
  if (!stream_) return;
  if (paused) {
    SDL_PauseAudioStreamDevice(stream_);
  } else {
    SDL_ResumeAudioStreamDevice(stream_);
  }
}

double Audio::queued_seconds() const {
  if (!stream_ || rate_ <= 0) return 0;
  return SDL_GetAudioStreamQueued(stream_) / 4.0 / rate_;
}

}  // namespace p64
