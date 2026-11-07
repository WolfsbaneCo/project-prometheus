// Keeps two linked GBA games together over the internet, as a real cable keeps two consoles.
//
// gpSP plays the link cable loosely: each game runs on its own, and when the other's message for a
// transfer hasn't come yet, the game goes on without it. Over a steady connection that's fine
// (checked to 300 ms each way with jitter). But when the connection stalls for a second or more (a
// VPN, a busy line) and then delivers everything at once, the game that went on can't catch up: it
// reads the other's messages late from then on, or acts on a stale picture of the other (taking the
// other game as still linked while it is re-opening its link to start a trade), and the link breaks
// ("Communication error", or both screens black). A real cable never stalls.
//
// So each game tells the other how many frames it has run, every few frames, for as long as they
// are linked, and a game that has run more than kWindow frames beyond the other (past the delay
// the connection had when they met) waits for it. In a stall both games stop, a frozen picture for
// its length, and carry on together. It also keeps two computers from drifting apart when one runs
// a little faster. The ticks travel apart from the core's messages, so the core sees nothing new.
#pragma once

#include <cstdint>

namespace p64 {

class LinkPacer {
 public:
  // Frames a game may run beyond the other, over the connection's own delay: about the delay
  // already checked to work (300 ms each way), and well inside gpSP's 128 queued messages.
  static constexpr int64_t kWindow = 30;
  // How often a game says how far it is (frames).
  static constexpr uint64_t kEvery = 4;

  // The other game has run `frames` frames since its link started.
  void heard(uint64_t frames, uint64_t mine) {
    if (!heard_ || frames < theirs_) {
      // The first word, or the other game's link started again: what the gap is now (their head
      // start, the connection's delay) is the baseline.
      base_ = static_cast<int64_t>(mine) - static_cast<int64_t>(frames);
      heard_ = true;
    }
    theirs_ = frames;
  }
  // This game, at `mine` frames, has run too far beyond the other and should wait.
  bool behind(uint64_t mine) const {
    return heard_ && !gave_up_ && static_cast<int64_t>(mine) - static_cast<int64_t>(theirs_) - base_ > kWindow;
  }
  // After a wait too long to be a stall (the other game gone without a word), stop holding this
  // game, so the link's own error can show.
  void give_up() { gave_up_ = true; }
  // For the log: whether the other has been heard, its frames, and how far this game is past the
  // baseline gap (positive: ahead; the game waits beyond kWindow).
  bool known() const { return heard_; }
  uint64_t theirs() const { return theirs_; }
  int64_t base() const { return base_; }
  int64_t lead(uint64_t mine) const { return static_cast<int64_t>(mine) - static_cast<int64_t>(theirs_) - base_; }
  void reset() {
    heard_ = gave_up_ = false;
    theirs_ = 0;
    base_ = 0;
  }

 private:
  bool heard_ = false, gave_up_ = false;
  uint64_t theirs_ = 0;
  int64_t base_ = 0;
};

}  // namespace p64
