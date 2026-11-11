// Project Prometheus relay: the meeting point for online play in every Project Prometheus app
// (Prometheus 64 now; others later, such as trading between GBA games), so nobody has to change
// router settings. It belongs to one group of friends: only apps holding the group's key file (the
// GROUP_KEY secret, made when the relay is set up) can use it, and joining also takes the host's
// room code: the key gets you into the group, the code into one room. Each app has its own rooms
// (`app`), so one key serves them all. Rooms are never listed. The relay only passes messages
// between the players' apps; it never sees game files and keeps nothing once a room closes. One
// Durable Object per room.
//
//   wss  /v1/host?app=..&key=..&v=2&name=..           open a room; the first message names its code
//   wss  /v1/rooms/<CODE>?app=..&key=..&v=2&name=..   join a room
//
// Coming back after a dropped connection (an app whose link broke takes it up again where it
// stopped): the welcome carries a `token`, known only to that player's app. A host comes back
// with /v1/host?..&code=<CODE>&resume=<token> and gets its room and code again; a guest with
// /v1/rooms/<CODE>?..&resume=<token>&id=<its id> gets its id again. A connection the relay still
// held for that player (it hadn't noticed the break) is replaced, and the others hear a `join`
// with the id they already know. Without these, nothing changes for older apps.
//
// Messages from the apps are binary: the first byte names the recipient (255 = everyone else),
// the rest is passed on unchanged, with that first byte replaced by the sender's player id
// (0 = host). [254, 1] from the host is for the relay: the game started, nobody else can join.
// Messages from the relay itself are JSON text: welcome, join, leave.

const PROTOCOL = 2;
const CODE_CHARS = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const CODE_LENGTH = 5;
const MAX_PLAYERS = 4; // computers per room, the host included
const BROADCAST = 255;
const TO_RELAY = 254;

function randomCode() {
  const bytes = crypto.getRandomValues(new Uint8Array(CODE_LENGTH));
  return Array.from(bytes, (b) => CODE_CHARS[b % CODE_CHARS.length]).join("");
}

// Sends unless the socket is already closing (players can leave at the same moment).
function send(ws, data) {
  try {
    ws.send(data);
  } catch {
    // closed
  }
}

function param(url, name, max) {
  return (url.searchParams.get(name) || "").trim().slice(0, max);
}

function keyMatches(given, expected) {
  const enc = new TextEncoder();
  const a = enc.encode(given || ""), b = enc.encode(expected || "");
  return b.byteLength > 0 && a.byteLength === b.byteLength && crypto.subtle.timingSafeEqual(a, b);
}

function stub(env, name) {
  return env.ROOMS.get(env.ROOMS.idFromName(name));
}

// A request to a Durable Object: the room code and what is wanted travel in headers.
function inner(request, code, role) {
  const headers = new Headers(request.headers);
  headers.set("X-Room-Code", code);
  headers.set("X-Room-Role", role);
  return new Request(request.url, { headers });
}

function randomToken() {
  return Array.from(crypto.getRandomValues(new Uint8Array(12)), (b) => b.toString(16).padStart(2, "0")).join("");
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/") return new Response("Project Prometheus relay\n");

    const isHost = url.pathname === "/v1/host";
    const join = url.pathname.match(/^\/v1\/rooms\/([A-Z0-9]{5})$/);
    if (!isHost && !join) return new Response("Not found", { status: 404 });
    if (!env.GROUP_KEY) return new Response("This relay has no group key. Set it up again from the app", { status: 503 });
    if (!keyMatches(url.searchParams.get("key"), env.GROUP_KEY)) {
      return new Response("This key file doesn't open this relay", { status: 401 });
    }
    if (Number(url.searchParams.get("v")) !== PROTOCOL) {
      return new Response("This version of the app is too old or too new for the relay", { status: 400 });
    }
    const app = url.searchParams.get("app") || "";
    if (!/^[a-z0-9-]{1,32}$/.test(app)) return new Response("Which app is this?", { status: 400 });

    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected a WebSocket connection", { status: 426 });
    }
    if (join) return stub(env, `${app}:${join[1]}`).fetch(inner(request, join[1], "guest"));
    // A host coming back asks for its own code again: it gets it if the room is empty by now, or
    // still holds the connection this host had (its token proves it).
    const wanted = param(url, "code", CODE_LENGTH);
    if (new RegExp(`^[${CODE_CHARS}]{${CODE_LENGTH}}$`).test(wanted)) {
      const response = await stub(env, `${app}:${wanted}`).fetch(inner(request, wanted, "host"));
      if (response.status !== 409) return response;
    }
    // A new room: try a few random codes until one is free.
    for (let attempt = 0; attempt < 8; attempt++) {
      const code = randomCode();
      const response = await stub(env, `${app}:${code}`).fetch(inner(request, code, "host"));
      if (response.status !== 409) return response;
    }
    return new Response("Could not open a room, try again", { status: 503 });
  },
};

export class Room {
  constructor(ctx) {
    this.ctx = ctx; // keep the constructor light: it runs again after hibernation
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = request.headers.get("X-Room-Code");
    const role = request.headers.get("X-Room-Role");
    let sockets = this.ctx.getWebSockets();
    let players = sockets.map((ws) => ws.deserializeAttachment());

    // A player coming back after a dropped connection: the connection the relay still holds
    // for them, if any (found by the token only their app knows).
    const resume = param(url, "resume", 64);
    const old = resume ? sockets.find((ws) => ws.deserializeAttachment().token === resume) : undefined;
    const was = old ? old.deserializeAttachment() : undefined;

    let id = 0;
    let started = false;
    if (role === "host") {
      if (was && was.id === 0) {
        started = !!was.started;
      } else if (players.some((p) => p.id === 0)) {
        return new Response("Room code in use", { status: 409 });
      } else {
        // No host: what is left are guests the relay is still sending away from the room the
        // host just lost (its connection dropped). The room starts again, empty.
        for (const ws of sockets) {
          try {
            ws.close(4000, "The host left the game");
          } catch {
            // closed
          }
        }
        sockets = [];
        players = [];
      }
    } else if (was && was.id !== 0) {
      id = was.id;
    } else {
      const host = players.find((p) => p.id === 0);
      if (!host) return new Response("This game isn't open any more", { status: 404 });
      if (host.started) return new Response("This game has already started", { status: 423 });
      if (sockets.length >= MAX_PLAYERS) return new Response("This game is full", { status: 403 });
      const used = new Set(players.map((p) => p.id));
      // The id it had before, when it asks for it and it's free; otherwise the lowest free one.
      const asked = Number(param(url, "id", 1));
      id = asked >= 1 && asked < MAX_PLAYERS && !used.has(asked) ? asked : 1;
      while (used.has(id)) id++;
    }
    if (was && was.id === id) {
      // Replaced quietly: nobody is told this player left.
      old.serializeAttachment({ ...was, replaced: true });
      try {
        old.close(4001, "Replaced by a new connection");
      } catch {
        // closed
      }
      sockets = sockets.filter((ws) => ws !== old);
      players = sockets.map((ws) => ws.deserializeAttachment());
    }

    const name = param(url, "name", 24) || "Player";
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    const token = randomToken();
    const me = { id, name, token };
    if (started) me.started = true;
    server.serializeAttachment(me);
    server.send(
      JSON.stringify({ t: "welcome", code, you: id, token, players: players.map((p) => ({ id: p.id, name: p.name })) }),
    );
    for (const ws of sockets) send(ws, JSON.stringify({ t: "join", id, name }));
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message === "string") return; // the apps only send binary messages
    const data = new Uint8Array(message);
    if (data.length < 2) return;
    const from = ws.deserializeAttachment();
    const to = data[0];
    if (to === TO_RELAY) {
      if (from.id === 0 && data[1] === 1) ws.serializeAttachment({ ...from, started: true });
      return;
    }
    const out = data.slice();
    out[0] = from.id;
    for (const other of this.ctx.getWebSockets()) {
      if (other === ws) continue;
      if (to === BROADCAST || other.deserializeAttachment().id === to) send(other, out);
    }
  }

  webSocketClose(ws) {
    this.leave(ws);
  }

  webSocketError(ws) {
    this.leave(ws);
  }

  leave(ws) {
    const gone = ws.deserializeAttachment();
    if (gone.replaced) return; // its player came back on a new connection
    for (const other of this.ctx.getWebSockets()) {
      if (other === ws) continue;
      if (gone.id === 0) {
        try {
          other.close(4000, "The host left the game");
        } catch {
          // closed
        }
      } else {
        send(other, JSON.stringify({ t: "leave", id: gone.id }));
      }
    }
    try {
      ws.close(1000, "Left");
    } catch {
      // already closed
    }
  }
}
