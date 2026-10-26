// Prometheus 64 relay: the meeting point for online play, so nobody has to change router
// settings. A host opens a room and gets a short code; friends join with the code. The relay only
// passes messages between the players' apps (controller inputs, the host's save, pings). It never
// sees game files, keeps nothing once a room closes, and runs one Durable Object per room.
//
//   wss://<relay>/v1/host?name=..&v=1        open a room; the first message names its code
//   wss://<relay>/v1/rooms/<CODE>?name=..&v=1 join a room
//
// Messages from the apps are binary: the first byte names the recipient (255 = everyone else),
// the rest is passed on unchanged, with that first byte replaced by the sender's player id
// (0 = host). Messages from the relay itself are JSON text: welcome, join, leave.

const PROTOCOL = 1;
const CODE_CHARS = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"; // no 0/O, 1/I/L: easy to read out loud
const CODE_LENGTH = 5;
const MAX_PLAYERS = 4; // computers per room, the host included
const BROADCAST = 255;

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

function playerName(url) {
  const name = (url.searchParams.get("name") || "").trim().slice(0, 24);
  return name || "Player";
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.pathname === "/") return new Response("Prometheus 64 relay\n");

    const isHost = url.pathname === "/v1/host";
    const join = url.pathname.match(/^\/v1\/rooms\/([A-Z0-9]{5})$/);
    if (!isHost && !join) return new Response("Not found", { status: 404 });
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("Expected a WebSocket connection", { status: 426 });
    }
    if (Number(url.searchParams.get("v")) !== PROTOCOL) {
      return new Response("This version of Prometheus 64 is too old or too new for the relay", { status: 400 });
    }

    if (join) {
      const code = join[1];
      return env.ROOMS.get(env.ROOMS.idFromName(code)).fetch(roomRequest(request, code, "guest"));
    }
    // A new room: try a few random codes until one is free.
    for (let attempt = 0; attempt < 8; attempt++) {
      const code = randomCode();
      const response = await env.ROOMS.get(env.ROOMS.idFromName(code)).fetch(roomRequest(request, code, "host"));
      if (response.status !== 409) return response;
    }
    return new Response("Could not open a room, try again", { status: 503 });
  },
};

function roomRequest(request, code, role) {
  const headers = new Headers(request.headers);
  headers.set("X-Room-Code", code);
  headers.set("X-Room-Role", role);
  return new Request(request.url, { headers });
}

export class Room {
  constructor(ctx) {
    this.ctx = ctx; // keep the constructor light: it runs again after hibernation
  }

  async fetch(request) {
    const url = new URL(request.url);
    const code = request.headers.get("X-Room-Code");
    const role = request.headers.get("X-Room-Role");
    const sockets = this.ctx.getWebSockets();
    const players = sockets.map((ws) => ws.deserializeAttachment());

    let id = 0;
    if (role === "host") {
      if (sockets.length > 0) return new Response("Room code in use", { status: 409 });
    } else {
      if (!players.some((p) => p.id === 0)) return new Response("No game is open with this code", { status: 404 });
      if (sockets.length >= MAX_PLAYERS) return new Response("This game is full", { status: 403 });
      const used = new Set(players.map((p) => p.id));
      id = 1;
      while (used.has(id)) id++;
    }

    const name = playerName(url);
    const [client, server] = Object.values(new WebSocketPair());
    this.ctx.acceptWebSocket(server);
    server.serializeAttachment({ id, name });
    server.send(JSON.stringify({ t: "welcome", code, you: id, players: players.map((p) => ({ id: p.id, name: p.name })) }));
    for (const ws of sockets) send(ws, JSON.stringify({ t: "join", id, name }));
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message === "string") return; // the apps only send binary messages
    const data = new Uint8Array(message);
    if (data.length < 2) return;
    const from = ws.deserializeAttachment();
    const to = data[0];
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
