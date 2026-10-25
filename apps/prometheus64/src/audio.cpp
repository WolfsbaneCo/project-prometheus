#include "audio.hpp"

namespace p64 {

bool Audio::open(double sample_rate) {
  close();
  SDL_AudioSpec spec{SDL_AUDIO_S16, 2, static_cast<int>(sample_rate + 0.5)};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if (!stream_) return false;
  rate_ = sample_rate;
  SDL_ResumeAudioStreamDevice(stream_);
  return true;
}

void Audio::close() {
  if (stream_) SDL_DestroyAudioStream(stream_);
  stream_ = nullptr;
}

void Audio::push(const int16_t* stereo, std::size_t frames) {
  if (!stream_) return;
  // Keep latency bounded: if the game runs slightly faster than the sound card (a 60 Hz
  // screen against the N64's 59.94 Hz), drop the occasional batch instead of lagging behind.
  if (queued_seconds() > 0.15) return;
  SDL_PutAudioStreamData(stream_, stereo, static_cast<int>(frames * 4));
}

void Audio::clear() {
  if (stream_) SDL_ClearAudioStream(stream_);
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
