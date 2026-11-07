#include "local_link.hpp"

namespace p64 {

namespace {
LocalLink* g_local = nullptr;  // the cores' callbacks have no user pointer
constexpr uint16_t kFirst = 0, kSecond = 1, kEveryone = 0xFFFF;
}  // namespace

LocalLink::~LocalLink() { stop(); }

void LocalLink::start(const retro_netpacket_callback* first, const retro_netpacket_callback* second) {
  stop();
  if (!first || !second || !first->start || !second->start) return;
  g_local = this;
  first_ = first;
  second_ = second;
  first_->start(kFirst, send_first, poll_first);
  second_->start(kSecond, send_second, poll_second);
  if (first_->connected) first_->connected(kSecond);  // the host hears the guest arrive, as over the relay
}

void LocalLink::stop() {
  if (first_) {
    if (first_->disconnected) first_->disconnected(kSecond);
    if (first_->stop) first_->stop();
    if (second_ && second_->stop) second_->stop();
  }
  first_ = second_ = nullptr;
  pace_first_.reset();
  pace_second_.reset();
  frames_first_ = frames_second_ = 0;
  to_first_.clear();
  to_second_.clear();
  if (g_local == this) g_local = nullptr;
}

void LocalLink::deliver(const retro_netpacket_callback* to, std::deque<Message>& queue, uint16_t from) {
  while (to && to->receive && !queue.empty() && queue.front().due <= now_) {
    Message m = std::move(queue.front());
    queue.pop_front();
    if (m.tick) {
      if (to == first_) pace_first_.heard(m.frames, frames_first_);
      else pace_second_.heard(m.frames, frames_second_);
      continue;
    }
    to->receive(m.data.data(), m.data.size(), from);
  }
}

void LocalLink::queue(std::deque<Message>& q, const void* buf, size_t len) {
  uint64_t due = now_;
  if (delay_ || jitter_ || stall_every_) {
    rng_ = rng_ * 6364136223846793005ull + 1442695040888963407ull;
    due += static_cast<uint64_t>(delay_) + (jitter_ ? (rng_ >> 33) % static_cast<uint64_t>(jitter_ + 1) : 0);
    // Inside a stall, a message waits for its end.
    if (stall_every_ > 0 && stall_length_ > 0 && due % static_cast<uint64_t>(stall_every_) < static_cast<uint64_t>(stall_length_))
      due += static_cast<uint64_t>(stall_length_) - due % static_cast<uint64_t>(stall_every_);
    if (!q.empty() && q.back().due > due) due = q.back().due;  // in order, as over one connection
  }
  const auto* b = static_cast<const uint8_t*>(buf);
  q.push_back({due, std::vector<uint8_t>(b, b + len)});
}

void LocalLink::ran(uint64_t& frames, std::deque<Message>& to_other) {
  ++frames;
  if (!(delay_ || stall_every_) || frames % LinkPacer::kEvery) return;
  queue(to_other, nullptr, 0);
  to_other.back().tick = true;
  to_other.back().frames = frames;
}

void LocalLink::before_first() {
  if (!first_) return;
  deliver(first_, to_first_, kSecond);
  if (first_->poll) first_->poll();
}

void LocalLink::before_second() {
  if (!second_) return;
  deliver(second_, to_second_, kFirst);
  if (second_->poll) second_->poll();
}

void RETRO_CALLCONV LocalLink::send_first(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  if (!g_local || !buf || (client_id != kSecond && client_id != kEveryone)) return;
  g_local->queue(g_local->to_second_, buf, len);
}

void RETRO_CALLCONV LocalLink::send_second(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  if (!g_local || !buf || (client_id != kFirst && client_id != kEveryone)) return;
  g_local->queue(g_local->to_first_, buf, len);
}

// The cores ask for what has arrived: hand it over at once.
void RETRO_CALLCONV LocalLink::poll_first() {
  if (g_local) g_local->deliver(g_local->first_, g_local->to_first_, kSecond);
}

void RETRO_CALLCONV LocalLink::poll_second() {
  if (g_local) g_local->deliver(g_local->second_, g_local->to_second_, kFirst);
}

}  // namespace p64
