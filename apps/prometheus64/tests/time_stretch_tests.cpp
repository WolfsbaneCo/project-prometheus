// Fast-forward sound: sped up without raising its pitch (src/time_stretch.cpp).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "time_stretch.hpp"
using namespace p64;
int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

// A 440 Hz tone, stereo, fed in 1/60 s chunks as a game makes it; the output as one stream.
static std::vector<int16_t> run(double speed, double seconds) {
  const double rate = 44100;
  TimeStretch ts(rate);
  ts.set_speed(speed);
  const std::size_t total = static_cast<std::size_t>(rate * seconds), chunk = 735;
  std::vector<int16_t> in(2 * total), out;
  for (std::size_t i = 0; i < total; ++i) {
    const int16_t v = static_cast<int16_t>(10000 * std::sin(2 * 3.14159265358979 * 440 * i / rate));
    in[2 * i] = in[2 * i + 1] = v;
  }
  for (std::size_t at = 0; at < total; at += chunk) {
    const auto o = ts.process(&in[2 * at], std::min(chunk, total - at));
    out.insert(out.end(), o.begin(), o.end());
  }
  return out;
}

int main() {
  for (double speed : {2.0, 4.0, 8.0}) {
    const auto out = run(speed, 4.0);
    const std::size_t frames = out.size() / 2;
    const double expected = 44100 * 4.0 / speed;
    // About 1/speed as long, give or take one piece (25 ms) at either end.
    CHECK(frames > expected - 1103 && frames < expected + 1103);
    // Still 440 Hz: count upward zero crossings over the middle of the output.
    int ups = 0;
    const std::size_t from = frames / 4, to = 3 * frames / 4;
    for (std::size_t i = from + 1; i < to; ++i) ups += out[2 * (i - 1)] < 0 && out[2 * i] >= 0;
    const double hz = ups / ((to - from) / 44100.0);
    std::printf("speed %.0fx: %zu frames (expected %.0f), %.1f Hz\n", speed, frames, expected, hz);
    CHECK(std::fabs(hz - 440) < 440 * 0.03);
    // No clicks: no jump between samples much bigger than the tone's own steepest step (~630).
    int worst = 0;
    for (std::size_t i = 1; i < frames; ++i) worst = std::max(worst, std::abs(out[2 * i] - out[2 * (i - 1)]));
    std::printf("  largest step %d\n", worst);
    CHECK(worst < 1200);
  }
  // At 1x the sound passes through unchanged.
  TimeStretch ts;
  const int16_t pair[4] = {100, -100, 200, -200};
  const auto same = ts.process(pair, 2);
  CHECK(same.size() == 4 && same[0] == 100 && same[3] == -200);
  std::printf("%s (%d failures)\n", fails ? "FAILED" : "all passed", fails);
  return fails != 0;
}
