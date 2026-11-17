# 0012: Layers that keep an online game going

- **Status:** Accepted (implemented in 0.4.0).
- **Date:** 2026-11-17
- **Owner's ask:** "build multiple layered mechanisms to ensure the connection in N64 and GBA never fails".

No single connection can be made never to fail: a home router, a mobile network, an internet provider's block, Cloudflare itself can each drop it. What can be done is to make a failure of any one piece cost nothing, or as little as possible, and to say exactly what happened when everything fails at once. Each layer below covers a different way it breaks; they apply to GBA and Game Boy link play (`link.cpp`) and to N64 online play (`netplay.cpp`).

| Layer | What it covers | Where |
|---|---|---|
| 1. Name lookup that gets around blocks | An internet provider that blocks `workers.dev` names (Indonesia): the relay is looked up again over DNS-over-HTTPS (Cloudflare, Google, Quad9), and the lookup that worked is tried first next time | `net.cpp` (0.2.11) |
| 2. A dead line noticed in 6 seconds | A connection that dies without a word (Wi-Fi roaming, a VPN, a router): the relay answers a WebSocket ping every 2 seconds; 6 seconds of silence and the connection is replaced | `net.cpp`, `kSilentMs` |
| 3. Reconnect without losing anything | Every message in a game carries a number and is kept (GBA: 4096, N64: 8192); after reconnecting, each side says the last number it got and the other sends the rest. The game holds where it is meanwhile (N64: the lockstep waits for inputs; GBA: the link waits) and carries on as if nothing happened. 45 seconds to come back | `link.cpp` (0.2.13), `netplay.cpp` (0.4.0) |
| 4. The relay remembers who dropped | A player whose connection closed is remembered for two minutes, so it gets its id back, even in an N64 game that has started (which turns newcomers away); guests sent away when the host dropped come back as themselves | `relay/src/index.js` (0.4.0; **Update relay** puts it on the group's account) |
| 5. Goodbyes | A player leaving on purpose says so (over the relay and the direct path, and over a new connection if the old one is down), so nobody waits 45 seconds for someone who isn't coming back | `link.cpp`, `netplay.cpp` |
| 6. A second path, straight between the computers | UDP between the two apps, found with public STUN servers and hole punching (`direct.cpp`). Every numbered message goes over both paths; the first copy is used, the second dropped as already had; messages that arrive out of order wait a moment (1.5 s) for the missing one before it's asked for again. While either path works, the game doesn't stop at all: a relay that drops, stalls or is blocked mid-game costs nothing | `direct.cpp`, `link.cpp`, `netplay.cpp` |
| 7. A log of every layer | Both computers log each path (relay heard, direct path up and its round trip, reconnections, messages sent again, duplicates, which path delivered first), in UTC, so the two logs line up | `link_monitor.cpp`, `app_online.cpp` |

## The direct path

- **Finding each other:** each app opens one UDP socket and asks public STUN servers (Cloudflare's, Google's) which address the internet sees it at. It sends the other app, through the relay, every address it might be reached at: that one, its address on the home network, and 127.0.0.1 (two apps on one computer). Both send probes to all of the other's addresses four times a second until one gets through in either direction; the router's door that each probe opens lets the other's replies in. Once up, a keep-alive every second measures the round trip; 5 seconds without a word and the path is down (probing starts again).
- **Safety:** every datagram carries both apps' random tokens, new for each link and exchanged through the relay (which the group's key protects); anything else is ignored. Only the same messages as over the relay travel, so the direct path can't do anything the relay couldn't.
- **When it can't work:** two routers that both change the port for every destination ("symmetric NAT", common on mobile and shared connections) can't be punched through without a server in the middle. Then the relay carries the game alone, as before 0.4.0. A TURN server (a relay for UDP) would cover that case but needs an account and money; the relay already plays that part.
- **Windows Firewall** may ask once whether the app may use the network. Allowing it lets the direct path accept the other app's first probe; without it, the path usually still opens from this side's probes.

## Checked (in the bench, both apps on one computer, the relay behind proxies that add 60 to 150 ms each way)

- **GBA, a Cable Club trade:** the host's relay connection cut twice and the guest's blackholed (open but silent) mid-trade; the direct path carried the link throughout: 0.0 s held in every 5-second window, the trade went on.
- **N64 (the network bench, 70 s at 60 frames a second):** the same cuts and blackhole: 0 pauses, no desync. With the direct path switched off (`P64_DIRECT_MUTE`, a test switch): a cut cost one pause of 0.35 s; a silent line cost the 6 seconds it takes to notice; no desync either way.
- **The relay:** a guest coming back to a started game with its token gets its id; a stranger's token and a used token don't (`relay/test/relay.test.mjs`).

## Not done

- The direct path is IPv4 only; IPv6 (where mobile networks often allow direct connections that IPv4 doesn't) would be a next step.
- PSP games don't play online.
