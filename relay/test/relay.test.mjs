// Checks the relay against a running copy: `npm run dev` in one terminal, `npm test` in another.
// RELAY=ws://127.0.0.1:8787 by default.
import assert from "node:assert/strict";

const RELAY = process.env.RELAY || "ws://127.0.0.1:8787";

function open(path) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`${RELAY}${path}`);
    ws.binaryType = "arraybuffer";
    const queue = [];
    const waiters = [];
    ws.onmessage = (e) => {
      const msg = typeof e.data === "string" ? JSON.parse(e.data) : new Uint8Array(e.data);
      const w = waiters.shift();
      if (w) w(msg);
      else queue.push(msg);
    };
    ws.next = () => new Promise((r) => (queue.length ? r(queue.shift()) : waiters.push(r)));
    ws.closed = new Promise((r) => (ws.onclose = (e) => r(e)));
    ws.onopen = () => resolve(ws);
    ws.onerror = () => reject(new Error(`could not open ${path}`));
  });
}

const host = await open("/v1/host?name=Alex&v=1");
const welcome = await host.next();
assert.equal(welcome.t, "welcome");
assert.equal(welcome.you, 0);
assert.match(welcome.code, /^[A-Z2-9]{5}$/);
const code = welcome.code;

const guest = await open(`/v1/rooms/${code}?name=Sam&v=1`);
const gw = await guest.next();
assert.equal(gw.you, 1);
assert.deepEqual(gw.players, [{ id: 0, name: "Alex" }]);
assert.deepEqual(await host.next(), { t: "join", id: 1, name: "Sam" });

// Broadcast from the guest arrives at the host with the sender's id in the first byte.
guest.send(new Uint8Array([255, 7, 8, 9]));
assert.deepEqual(Array.from(await host.next()), [1, 7, 8, 9]);
// Addressed from the host to player 1.
host.send(new Uint8Array([1, 42]));
assert.deepEqual(Array.from(await guest.next()), [0, 42]);

// A wrong code and an old protocol version are refused.
await assert.rejects(open("/v1/rooms/ZZZZZ?name=X&v=1"));
await assert.rejects(open(`/v1/rooms/${code}?name=X&v=99`));

// Fill the room: two more guests, then a fifth computer is refused.
const g2 = await open(`/v1/rooms/${code}?name=Kim&v=1`);
const g3 = await open(`/v1/rooms/${code}?name=Lee&v=1`);
assert.equal((await g3.next()).you, 3);
await assert.rejects(open(`/v1/rooms/${code}?name=Max&v=1`));

// A guest leaving is announced; the host leaving closes the room for everyone.
g3.close();
const left = [await host.next(), await host.next(), await host.next()].find((m) => m.t === "leave");
assert.ok(left, "host hears about the guest leaving");
host.close();
const closed = await guest.closed;
assert.equal(closed.code, 4000);
await g2.closed;

console.log("relay: all checks passed");
process.exit(0);
