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
  to_first_.clear();
  to_second_.clear();
  if (g_local == this) g_local = nullptr;
}

void LocalLink::deliver(const retro_netpacket_callback* to, std::deque<std::vector<uint8_t>>& queue, uint16_t from) {
  while (to && to->receive && !queue.empty()) {
    std::vector<uint8_t> p = std::move(queue.front());
    queue.pop_front();
    to->receive(p.data(), p.size(), from);
  }
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
  const auto* b = static_cast<const uint8_t*>(buf);
  g_local->to_second_.emplace_back(b, b + len);
}

void RETRO_CALLCONV LocalLink::send_second(int /*flags*/, const void* buf, size_t len, uint16_t client_id) {
  if (!g_local || !buf || (client_id != kFirst && client_id != kEveryone)) return;
  const auto* b = static_cast<const uint8_t*>(buf);
  g_local->to_first_.emplace_back(b, b + len);
}

// The cores ask for what has arrived: hand it over at once.
void RETRO_CALLCONV LocalLink::poll_first() {
  if (g_local) g_local->deliver(g_local->first_, g_local->to_first_, kSecond);
}

void RETRO_CALLCONV LocalLink::poll_second() {
  if (g_local) g_local->deliver(g_local->second_, g_local->to_second_, kFirst);
}

}  // namespace p64
