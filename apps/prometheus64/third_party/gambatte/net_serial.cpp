// Project Prometheus: the Game Boy link cable over the frontend's link.
//
// On a real cable the console that starts a transfer (the master, its own clock) shifts its byte
// out and, in the same moment, the other console's byte in; the other (the slave, armed with an
// external clock) gets the master's byte and its serial interrupt. Over a network the slave's byte
// is a round trip away, so a master transfer here waits for it: the game goes on running (it is
// waiting for the transfer to end, as on the console) and the transfer ends when the answer comes.
//
// Messages: {1, byte, fast} a master's byte; {2, byte} the slave's answer. A slave that isn't armed
// answers 0xFF, as a console that isn't transferring does. Two masters at once each take the
// other's byte as the answer (as Gambatte's own link did).
#include "net_serial.h"

#include <cstring>
#include <deque>
#include <utility>

namespace {

enum : unsigned char { kMaster = 1, kAnswer = 2 };

retro_netpacket_send_t g_send = nullptr;
retro_netpacket_poll_receive_t g_poll = nullptr;
bool g_peer = false;  // the other console is on the cable

// This console's side.
unsigned char g_sb = 0xFF, g_sc = 0;  // the serial registers, as the game last left them
bool g_waiting = false;               // a master transfer waiting for its answer
bool g_answered = false;
unsigned char g_answer = 0xFF;
std::deque<std::pair<unsigned char, bool> > g_incoming;  // master bytes for the next slave transfer

bool armed() { return (g_sc & 0x81) == 0x80; }

void post(const unsigned char* data, size_t len) {
  if (g_send) g_send(RETRO_NETPACKET_RELIABLE | RETRO_NETPACKET_FLUSH_HINT, data, len, RETRO_NETPACKET_BROADCAST);
}

void RETRO_CALLCONV on_start(uint16_t, retro_netpacket_send_t send_fn, retro_netpacket_poll_receive_t poll_fn) {
  g_send = send_fn;
  g_poll = poll_fn;
  g_peer = false;
  g_waiting = g_answered = false;
  g_incoming.clear();
}

void RETRO_CALLCONV on_receive(const void* buf, size_t len, uint16_t) {
  const unsigned char* m = static_cast<const unsigned char*>(buf);
  if (len < 2) return;
  g_peer = true;
  if (m[0] == kMaster && len >= 3) {
    if (g_waiting && !g_answered) {
      // Both clocked a transfer at once: the other's byte is this one's answer, and this one's
      // byte (already sent) is the other's.
      g_answered = true;
      g_answer = m[1];
      return;
    }
    const unsigned char answer[2] = {kAnswer, static_cast<unsigned char>(armed() ? g_sb : 0xFF)};
    post(answer, sizeof answer);
    if (armed()) g_incoming.push_back(std::make_pair(m[1], m[2] != 0));
  } else if (m[0] == kAnswer) {
    if (g_waiting && !g_answered) {
      g_answered = true;
      g_answer = m[1];
    }
  }
}

void RETRO_CALLCONV on_stop() {
  g_send = nullptr;
  g_poll = nullptr;
  g_peer = false;
  g_incoming.clear();
  if (g_waiting && !g_answered) {
    g_answered = true;  // the cable came out mid-transfer
    g_answer = 0xFF;
  }
}

bool RETRO_CALLCONV on_connected(uint16_t) {
  g_peer = true;
  return true;
}

void RETRO_CALLCONV on_disconnected(uint16_t) {
  g_peer = false;
  g_incoming.clear();
  if (g_waiting && !g_answered) {
    g_answered = true;
    g_answer = 0xFF;
  }
}

const retro_netpacket_callback kCallbacks = {on_start, on_receive, on_stop, nullptr, on_connected, on_disconnected,
                                             "prometheus-gb-link-1"};

}  // namespace

NetSerial::NetSerial() {}
NetSerial::~NetSerial() {}

void p64_link_register(retro_environment_t environ_cb) {
  if (environ_cb) environ_cb(RETRO_ENVIRONMENT_SET_NETPACKET_INTERFACE, const_cast<retro_netpacket_callback*>(&kCallbacks));
}

unsigned char NetSerial::send(unsigned char data, bool fastCgb) {
  g_answered = false;
  g_waiting = false;
  if (!g_send) return 0xFF;  // no cable
  const unsigned char m[3] = {kMaster, data, static_cast<unsigned char>(fastCgb ? 1 : 0)};
  post(m, sizeof m);
  g_waiting = true;
  return 0xFF;  // replaced by the answer when it comes (reply())
}

bool NetSerial::waiting() { return g_waiting; }

bool NetSerial::reply(unsigned char& in) {
  if (!g_waiting) return false;
  if (!g_answered && g_poll) g_poll();
  if (!g_answered && !g_send) {
    g_answered = true;  // the link stopped
    g_answer = 0xFF;
  }
  if (!g_answered) return false;
  in = g_answer;
  g_waiting = g_answered = false;
  return true;
}

bool NetSerial::check(unsigned char, unsigned char& in, bool& fastCgb) {
  if (g_incoming.empty()) return false;
  in = g_incoming.front().first;
  fastCgb = g_incoming.front().second;
  g_incoming.pop_front();
  return true;
}

void NetSerial::registers(unsigned char sb, unsigned char sc) {
  g_sb = sb;
  g_sc = sc;
}
