# Online play relay

A tiny Cloudflare Worker that passes controller inputs between the players of an online game. Each room is one Durable Object: the host opens it and gets a five-letter code, friends join with that code, and every message is forwarded to the others. The relay never sees a game or a save file, only inputs, the lobby and the host's in-game save at the start of a session.

It runs on the host's own free Cloudflare account. Nobody else's account or server is involved.

## Setting it up (players)

Nothing to do here: the app does it. In Prometheus 64 press **N** (Play online), then **Set up online play**, and follow the three steps on screen:

1. Make a free Cloudflare account (or sign in).
2. Click **Open the key page**. Cloudflare opens with the two permissions already chosen (Workers Scripts: Edit, Account Settings: Read). Scroll down, click **Continue to summary**, **Create Token**, then **Copy**.
3. Paste the key into the app and click **Set up**.

The app installs this relay as `prometheus64-relay` on the account's free `workers.dev` address (creating that address if the account has none yet) and remembers only the address. The key is used once and never stored. Friends who join need no account and no setup.

Invite codes look like `K7Q2M.alex`: the room code, then the host's `workers.dev` name, which tells the friend's app where the relay is.

The free plan is plenty: incoming WebSocket messages count 20 to 1 against the 100,000 requests a day, and the app sends inputs two frames at a time, so an evening of play uses a small part of it.

## Setting it up by hand (developers)

```sh
cd relay
npm ci
npx wrangler login
npx wrangler deploy
```

Then set `relay_subdomain = <your workers.dev name>` in the app's `settings.ini`, and the invite codes work as usual. (A relay somewhere else can be set with `relay_url = wss://…`; its invite codes are then just the five letters, and friends need the same `relay_url`.)

## Testing locally

```sh
npm run dev      # the relay on ws://127.0.0.1:8787
npm test         # in another terminal
```

Point the app at the local relay with the environment variable `P64_RELAY_URL=ws://127.0.0.1:8787`. The app's own test options run a whole session from the command line:

```sh
Prometheus64 game.z64 --online-host --online-start --online-exit-frame 900   # prints "online-invite CODE"
Prometheus64 --online-join CODE --online-exit-frame 900                      # needs the game in its library
```

Both print `online-ram <frame> <sha256>` at that frame; the two hashes must match. `P64_CLOUDFLARE_API=<url>` points the in-app setup at a stand-in for Cloudflare's API, used with `--online-setup <key>`.

## Protocol (version 1)

- `GET /` answers `Prometheus 64 relay` (the app checks this after setting up).
- `wss://…/v1/host?name=<name>&v=1` opens a room. `wss://…/v1/rooms/<CODE>?name=<name>&v=1` joins one.
- Refusals before the upgrade: 400 wrong version, 404 no such room, 403 room full, 409 code in use.
- Text messages are relay events, JSON: `welcome {code, you, players}`, `join {id, name}`, `leave {id}`. The host is always id 0. When the host leaves, the room closes (code 4000).
- Binary messages are the app's own: the first byte is the recipient (255 = everyone); the relay replaces it with the sender's id before forwarding.
