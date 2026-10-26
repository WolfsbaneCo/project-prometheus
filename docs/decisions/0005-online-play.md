# 0005: Online play

- **Status:** Accepted for Phase 4 (implemented in `apps/prometheus64` and `relay/`).
- **Date:** 2026-10-26
- **Brief sections:** 11, 14 (Phase 4)
- **Builds on:** [0001](0001-core-and-stack.md) option A, [0004](0004-player-architecture.md)
- **Owner's choices:** players far apart, over the internet; a relay on Cloudflare, set up from inside the app with very easy steps. Then, after trying it: a **key file** made by the setup, which anyone can add to their app to host or join (the installer never holds it); joining still takes the host's **game code** as well; nothing about online play is forced on someone just because they hold the key; and the key must work for **future Project Prometheus apps** too (trading Pokemon in a GBA app, for example).

## How it works

**Lockstep, inputs only** (brief 11). Every computer runs the same game from the same start. Each computer reads its own players a few frames ahead (the *input delay*) and sends those inputs to the others. A frame runs only once every player's input for it has arrived, so every copy computes exactly the same game. Every 300 frames each computer sends a fingerprint of the console's memory, and a mismatch is reported to everyone. No rollback.

| Part | Where | What it does |
|---|---|---|
| Relay | `relay/src/index.js` | Cloudflare Worker with one Durable Object per room: the group key, room codes, joining, forwarding |
| Setup | `apps/prometheus64/src/cloudflare.cpp` | Installs the relay on the group owner's account through Cloudflare's API, with a new group key |
| Key file | `app_online.cpp` | Saves, adds and forgets the group's `.prometheuskey` file |
| Network | `net.cpp` | libcurl: HTTPS requests and a WebSocket on its own thread |
| Lockstep | `netplay.cpp` | Lobby, game checks, start, inputs, fingerprints, pause, reset, end |
| Screens | `app_online.cpp` | Play online, setup, join, lobby, in-game notices |

## Choices and why

- **A relay, not direct connections.** Nobody has to open ports on their router (brief 11). A Cloudflare Worker is close to everyone, free at this size and needs no server to look after.
- **A group, set up once by one person.** The app opens Cloudflare's key page with the two needed permissions already chosen (Workers Scripts: Edit, Account Settings: Read), the player pastes the Cloudflare key, and the app finds or creates the account's `workers.dev` address, uploads the relay (built into the app from `relay/src/index.js`) with a new random group key as its `GROUP_KEY` secret, and switches it on. The Cloudflare key is used once and never saved. The calls match what `wrangler deploy` sends, read from wrangler's source.
- **The key file is the way in.** The setup offers to save the group's key file (relay address, group key, group name) to send to friends. Anyone who adds it (double-click, drop onto the window, or **Add key file**) can host and join; without it the relay refuses the connection. Setting up again makes a new key, which retires old copies. The installer never contains a key.
- **Key and code, both.** The key gets you into the group; each game still has its own five-character code from the host, needed to join (the owner's choice). Rooms are never listed. Holding the key changes nothing until someone chooses Host or Join.
- **One key for every Project Prometheus app.** The key file names no app (`"type": "Project Prometheus online key"`, `.prometheuskey`), the relay is `prometheus-relay`, and each app has its own rooms on it (`app=prometheus64`), so a future GBA app can trade over the same relay with the same key. The key is kept once per computer, beside the apps' own folders (`<app data>/Project Prometheus/online.prometheuskey`), so later apps find it without asking again.
- **Free plan.** SQLite-backed Durable Objects and the WebSocket Hibernation API work on the free plan. Incoming WebSocket messages count 20 to 1 against the 100,000 daily requests, so inputs go out two frames to a message: about 30 messages a second per computer.
- **Determinism.** Measured on Harvest Moon 64 before building anything: with GLideN64, the console's memory after 1,300 frames is identical between different window sizes, resolutions and texture settings, as long as nothing is read back from the graphics card. So online games force the GLideN64 renderer with the HLE RSP, `EnableCopyColorToRDRAM` off and the depth copy in software; everything else (resolution, textures, fill mode, sharpening) stays each player's own (brief 11.2). Checked afterwards between two copies of the app and between the Linux and Windows builds.
- **Same game, same version** (brief 11.1). A guest on another app or core version is turned away with a message naming both versions. Each player uses their own copy of the game: what has to match is the release, not the file. The host sends the SHA-256 of the game as the console sees it (byte order put right first), so a `.z64`, `.v64` or `.n64` copy, zipped or not, under any name, counts as the same game. A copy that would play differently can't keep in step, so the guest is listed with the reason and the host can't start until it's sorted: "Doesn't have …", "Has the Europe version of …, the host has the USA one", "Has another release of …" (a different revision), or "Has a changed copy of …" (a hack or a damaged file).
- **Ports.** The host's players take the first ports, then each guest's, in joining order; a computer can bring several players (brief 11.3). Each port gets a Rumble Pak if its player's controller rumbles, a Controller Pak otherwise, the same on every computer.
- **Saves** (brief 11.5). The host's in-game save is sent to the guests at the start and used by everyone. The host keeps saving as usual, with a backup taken first (`<game>/backups/before-online-<time>.srm`). Guests never write the session's save into their own library.
- **Save states off** (11.4): greyed out in the menu, F5/F7 explain why. Changing the renderer, textures or resolution is also refused mid-game, because it restarts the game.
- **Pause and reset are the host's** (11.7). The host's menu pauses everyone ("Paused by …"); a guest's menu doesn't hold up the others (that guest's controller goes neutral). Reset is scheduled for a frame ahead so it happens on the same frame everywhere.
- **Input delay.** Suggested from the slowest ping: half the round trip in frames, plus one, between 2 and 12. The host can change it in the lobby; ping is shown per player with the brief's guide (under 40 ms great, under 80 ms good, more is hard for fast games), and the lobby suggests a wired connection.
- **Leaving and arriving late.** If a guest leaves, the game ends for everyone else and the host keeps playing offline. If the host leaves, the room closes and the guests return to the library. Once the host starts, the relay turns away anyone else ("This game has already started"); someone turned away never ends the game for the others.

## Not done

- Rollback (out of scope in the brief).
- Giving guests a copy of the finished save (brief 11.6, optional).
- Spectators.
