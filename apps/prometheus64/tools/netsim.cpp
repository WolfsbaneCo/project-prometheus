// Two players' online code in one process, each paced at 60 frames a second like the app, with no
// emulator: measures pauses and delays from the network alone. A "game" state mixes every frame's
// inputs, so a desync shows up in the fingerprints. With the relay running locally (relay/: npm run
// dev), and optionally a proxy that adds lag in front of it for the guest:
//   p64_netsim ws://127.0.0.1:8787 ws://127.0.0.1:8787 60
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include "netplay.hpp"

using namespace p64;
static std::atomic<bool> g_quit{false};
static std::string g_code;
static std::atomic<int> g_have_code{0};

static void player(bool host, std::string url, int seconds, int busy_ms) {
  Netplay net;
  Netplay::GameInfo game;
  game.id = "SIM"; game.title = "Sim";
  if (host) net.host(url, "dev-group-key", "Host", game, 1, 0, "v");
  else {
    while (!g_have_code) SDL_Delay(10);
    net.join(url, "dev-group-key", g_code, "Guest", 1, 0, "v");
  }
  const char* who = host ? "host " : "guest";
  std::mt19937 rng(host ? 1 : 2);
  uint64_t state = 0;
  const double fps = 60.0;
  const uint64_t frame_ns = static_cast<uint64_t>(1e9 / fps);
  uint64_t acc = 0, last = SDL_GetTicksNS(), stalled_since = 0, stalled_total = 0, end_at = 0;
  int stalls = 0, yields = 0;
  bool running = false;
  while (!g_quit) {
    net.update();
    for (auto& e : net.take_events()) {
      if (e.kind == Netplay::Event::CheckGame) net.report_game(true, "");
      if (e.kind == Netplay::Event::LoadGame) net.report_ready();
      if (e.kind == Netplay::Event::Begin) { running = true; last = SDL_GetTicksNS(); acc = 0; end_at = last + seconds * 1'000'000'000ULL; }
      if (e.kind == Netplay::Event::Ended) { std::printf("%s ended: %s\n", who, e.text.c_str()); g_quit = true; }
      if (e.kind == Netplay::Event::Message) std::printf("%s: %s\n", who, e.text.c_str());
    }
    if (host && !net.code().empty() && !g_have_code) { g_code = net.code(); g_have_code = 1; }
    std::string why;
    if (host && net.phase() == Netplay::Phase::Lobby && net.can_start(why)) net.start({});
    const uint64_t now = SDL_GetTicksNS();
    acc += now - last; last = now;
    if (running && now > end_at) { g_quit = true; break; }
    if (running && acc >= frame_ns) {
      std::uniform_int_distribution<int> d(0, 0xFFFF);
      net.sample_local({PadState{static_cast<uint16_t>(d(rng)), 0, 0}});
      net.flush();
      if (!net.ready_to_run()) {
        net.flush(true);
        const uint64_t until = SDL_GetTicksNS() + frame_ns / 2;
        while (net.running() && !net.ready_to_run() && SDL_GetTicksNS() < until) { SDL_DelayNS(500'000); net.update(); }
      }
      if (net.ready_to_run() && net.take_yield()) { ++yields; acc = std::min(acc, frame_ns); }
      else if (!net.ready_to_run()) { if (!stalled_since) stalled_since = SDL_GetTicksNS(); acc = std::min(acc, frame_ns); }
      else {
        if (stalled_since) { stalled_total += SDL_GetTicksNS() - stalled_since; ++stalls; stalled_since = 0; }
        for (int p = 0; p < 2; ++p) state = state * 1099511628211ull + net.input(p).buttons + 1;
        if (busy_ms) SDL_DelayNS(static_cast<uint64_t>(busy_ms) * 1'000'000);  // pretend emulation time
        acc -= frame_ns;
        uint8_t ram[8]; std::memcpy(ram, &state, 8);
        net.frame_done(ram, 8);
        if (net.frame() % 600 == 0) {
          std::printf("%s frame %5u: %2d pauses, %4llu ms waiting, %d yields, delay %2d\n", who, net.frame(), stalls,
                      static_cast<unsigned long long>(stalled_total / 1'000'000), yields, net.my_delay());
          std::fflush(stdout);
          stalls = yields = 0; stalled_total = 0;
        }
      }
    }
    SDL_DelayNS(1'000'000);  // the app waits for vsync here; about a millisecond of slack per loop is enough
  }
  if (net.out_of_sync()) std::printf("%s: OUT OF SYNC\n", who);
  std::printf("%s final state %016llx at frame %u\n", who, static_cast<unsigned long long>(state), net.frame());
  net.leave();
}

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: netsim <host url> <guest url> <seconds> [busy ms]\n"); return 1; }
  SDL_Init(0);
  net::init();
  const int busy = argc > 4 ? std::atoi(argv[4]) : 3;
  std::thread h(player, true, argv[1], std::atoi(argv[3]), busy);
  std::thread g(player, false, argv[2], std::atoi(argv[3]), busy);
  h.join(); g.join();
  return 0;
}
