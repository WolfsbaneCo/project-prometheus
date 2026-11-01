#include "time_stretch.hpp"

#include <algorithm>
#include <cmath>

namespace p64 {

void TimeStretch::set_rate(double rate) {
  if (rate < 8000) rate = 8000;
  piece_ = static_cast<std::size_t>(rate * 0.025);
  overlap_ = static_cast<std::size_t>(rate * 0.008);
  search_ = static_cast<std::size_t>(rate * 0.006);
  reset();
}

void TimeStretch::reset() {
  in_.clear();
  tail_.clear();
  pos_ = 0;
  started_ = false;
}

// Where, within the search window from `at`, the input best continues the tail: the offset whose
// first `overlap_` samples correlate most with it (normalised, every second sample, mono).
std::size_t TimeStretch::best_offset(std::size_t at) const {
  std::size_t best = 0;
  double best_score = -1e30;
  for (std::size_t k = 0; k < search_; ++k) {
    double dot = 0, energy = 1e-9;
    for (std::size_t i = 0; i < overlap_; i += 2) {
      const std::size_t a = 2 * (at + k + i);
      const float x = in_[a] + in_[a + 1], y = tail_[2 * i] + tail_[2 * i + 1];
      dot += static_cast<double>(x) * y;
      energy += static_cast<double>(x) * x;
    }
    const double score = dot / std::sqrt(energy);
    if (score > best_score) {
      best_score = score;
      best = k;
    }
  }
  return best;
}

std::vector<int16_t> TimeStretch::process(const int16_t* stereo, std::size_t frames) {
  std::vector<int16_t> out;
  if (speed_ <= 1.0) {  // nothing to do: pass it on
    if (!in_.empty() || started_) reset();
    out.assign(stereo, stereo + 2 * frames);
    return out;
  }
  in_.insert(in_.end(), stereo, stereo + 2 * frames);
  const std::size_t hop = piece_ - overlap_;  // what each piece adds to the output
  auto put = [&](float v) { out.push_back(static_cast<int16_t>(std::clamp(v, -32768.0f, 32767.0f))); };
  for (;;) {
    const std::size_t at = static_cast<std::size_t>(pos_);
    if ((at + search_ + piece_) * 2 > in_.size()) break;  // wait for more sound
    std::size_t start = at;
    if (!started_) {
      // The first piece: no tail to match yet.
      tail_.assign(in_.begin() + static_cast<std::ptrdiff_t>(2 * start), in_.begin() + static_cast<std::ptrdiff_t>(2 * (start + overlap_)));
      started_ = true;
    } else {
      start = at + best_offset(at);
    }
    // Crossfade the tail into the piece's start, then the piece's middle as it is.
    for (std::size_t i = 0; i < overlap_; ++i) {
      const float w = (i + 0.5f) / static_cast<float>(overlap_);
      for (int c = 0; c < 2; ++c) put(tail_[2 * i + c] * (1.0f - w) + in_[2 * (start + i) + c] * w);
    }
    for (std::size_t i = overlap_; i < hop; ++i) {
      for (int c = 0; c < 2; ++c) put(in_[2 * (start + i) + c]);
    }
    // Its end becomes the next tail.
    tail_.assign(in_.begin() + static_cast<std::ptrdiff_t>(2 * (start + hop)), in_.begin() + static_cast<std::ptrdiff_t>(2 * (start + piece_)));
    pos_ += speed_ * static_cast<double>(hop);
  }
  // Drop what is behind the next read.
  const std::size_t done = std::min(static_cast<std::size_t>(pos_), in_.size() / 2);
  in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(2 * done));
  pos_ -= static_cast<double>(done);
  return out;
}

}  // namespace p64
