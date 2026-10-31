# Online play relay

A tiny Cloudflare Worker that passes messages between the players of an online game: controller inputs, the lobby, and the host's in-game save at the start of a session. It never sees a game file.

It serves **one group of friends** and **every Project Prometheus app**. Prometheus 64 uses it now; later apps (trading between GBA games, say) use the same relay with the same key, each in its own rooms.

- **The group key.** Only apps holding the group's key file can use the relay. The key is the `GROUP_KEY` secret on the Worker, made when the relay is set up.
- **The game code.** Joining a game also takes the host's five-character room code. The key gets you into the group; the code gets you into one game. Rooms are never listed.
- **Storage.** Each room is one Durable Object, and nothing is kept once it closes.

The relay runs on the group owner's own free Cloudflare account. Nobody else's account or server is involved.

## Setting it up (players)

Nothing to do here: the app does it. In Prometheus 64, press **N** (Play online), then **Set up**, and follow the three steps on screen:

1. Make a free Cloudflare account, or sign in.
2. Click **Open the key page**. Cloudflare opens with the two permissions already chosen (Workers Scripts: Edit, Account Settings: Read). Scroll down, click **Continue to summary**, then **Create Token**, then **Copy**.
3. Paste the Cloudflare key into the app and click **Set up**.

The app then does the rest:

- It finds or creates the account's free `workers.dev` address.
- It installs this relay there as `prometheus-relay`, with a new random group key as its secret. The Cloudflare key is used once and never stored.
- It offers to save the **group key file**, `*.prometheuskey`, to send to friends.

Setting up again replaces the group key, and older key files stop working.

### The key file

A small JSON file. Every Project Prometheus app reads it the same way:

```json
{
  "type": "Project Prometheus online key",
  "version": 1,
  "group": "Alex's group",
  "relay": "wss://prometheus-relay.alex.workers.dev",
  "key": "43 random characters"
}
```

To add it, double-click it, drop it onto the app, or click **Add key file**. It is kept once per computer, at `<app data>/Project Prometheus/online.prometheuskey` next to the apps' own folders, so every Project Prometheus app on that computer uses it. The installers never contain a key.

### Cost

The free plan is plenty. Incoming WebSocket messages count 20 to 1 against the 100,000 requests a day, and Prometheus 64 sends inputs two frames at a time, so an evening of play uses a small part of it.

## Setting it up by hand (developers)

```sh
cd relay
npm ci
npx wrangler login
npx wrangler deploy
npx wrangler secret put GROUP_KEY     # any long random string
```

Then write a key file by hand: the format is above. Use `wss://prometheus-relay.<your workers.dev name>.workers.dev` and the same string as the secret.

## Testing locally

```sh
npm run dev      # the relay on ws://127.0.0.1:8787, group key "dev-group-key"
npm test         # in another terminal
```

To point the app at the local relay, set the environment variables `P64_RELAY_URL=ws://127.0.0.1:8787` and `P64_RELAY_KEY=dev-group-key`, or add a key file with those values. The app's own test options run a whole session from the command line:

```sh
Prometheus64 game.z64 --online-host --online-start --online-exit-frame 900   # prints "online-invite CODE"
Prometheus64 --online-join CODE --online-exit-frame 900                      # needs the game in its library
```

Both print `online-ram <frame> <sha256>` at that frame, and the two hashes must match. Other test options:

- `P64_CLOUDFLARE_API=<url>` points the in-app setup at a stand-in for Cloudflare's API. Use it with `--online-setup <key>`.
- `--save-key <file>` saves the key file.

## Protocol (relay version 2)

- `GET /` answers `Project Prometheus relay`. The app checks this after setting up.
- `wss://…/v1/host?app=<app>&key=<group key>&v=2&name=<name>` opens a room.
- `wss://…/v1/rooms/<CODE>?app=<app>&key=<group key>&v=2&name=<name>` joins one.
- `app` names the app's own rooms: `prometheus64` for N64 online games, `prometheus-link` for GBA link play. The same code under another `app` is another room.
- Refusals before the upgrade:

  | Code | Meaning |
  |---|---|
  | 401 | Wrong or missing key |
  | 400 | Wrong relay version or app name |
  | 404 | No such room |
  | 403 | Room full (4 computers) |
  | 423 | Game already started |
  | 409 | Code in use |
  | 503 | The relay has no group key |

- **Text messages** are relay events, in JSON:
  - `welcome {code, you, players}`
  - `join {id, name}`
  - `leave {id}`

  The host is always id 0. When the host leaves, the room closes with code 4000.
- **Binary messages** belong to the app.
  - The first byte is the recipient (255 = everyone); the relay replaces it with the sender's id before forwarding.
  - `[254, 1]` from the host tells the relay itself that the game has started, so nobody else can join.
