// Speeds sound up without raising its pitch, for fast-forward: a game sped up 4x makes four
// seconds of sound each second, and played four times as fast it would sound four times as high.
// Instead the sound is cut into short pieces and only one piece in four is kept; each kept piece
// joins the last where their waveforms line up best, with a short crossfade (WSOLA, waveform-
// similarity overlap-add). Voices and music keep their pitch, just faster, as in VBA-M.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace p64 {

class TimeStretch {
 public:
  // `rate`: samples a second (each sample a stereo pair).
  explicit TimeStretch(double rate = 44100.0) { set_rate(rate); }
  void set_rate(double rate);
  // How many times faster the sound plays (1: as it comes; up to 8).
  void set_speed(double times) { speed_ = times < 1.0 ? 1.0 : times > 8.0 ? 8.0 : times; }
  double speed() const { return speed_; }
  // Takes stereo 16-bit sound; returns what is ready to play (stereo 16-bit), about frames/speed.
  std::vector<int16_t> process(const int16_t* stereo, std::size_t frames);
  void reset();

 private:
  std::size_t best_offset(std::size_t at) const;

  std::size_t piece_ = 1103, overlap_ = 353, search_ = 265;  // 25 ms, 8 ms, 6 ms at 44.1 kHz
  double speed_ = 1.0;
  double pos_ = 0;               // where the next piece is read from, in in_ (samples)
  std::vector<float> in_;        // sound taken in and not yet passed (stereo, interleaved)
  std::vector<float> tail_;      // the last piece's end, to crossfade into the next (stereo)
  bool started_ = false;
};

}  // namespace p64
