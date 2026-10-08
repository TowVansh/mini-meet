// Integration tests for the signalling server: start it on a random port,
// connect raw WebSocket clients and check the protocol end to end.

const test = require('node:test');
const assert = require('node:assert');
const WebSocket = require('ws');
const { server, wss } = require('../server/index');

let url;

test.before(async () => {
  await new Promise((r) => server.listen(0, r));
  url = `ws://localhost:${server.address().port}/ws`;
});

test.after(() => {
  for (const c of wss.clients) c.terminate();
  wss.close();
  server.close();
});

// Client wrapper that queues incoming messages so tests can await them in order.
function client() {
  const ws = new WebSocket(url);
  const queue = [];
  const waiters = [];
  ws.on('message', (d) => {
    const m = JSON.parse(d.toString());
    const w = waiters.shift();
    w ? w(m) : queue.push(m);
  });
  return {
    ws,
    open: () => new Promise((r) => ws.once('open', r)),
    send: (o) => ws.send(typeof o === 'string' ? o : JSON.stringify(o)),
    next: () => (queue.length ? Promise.resolve(queue.shift()) : new Promise((r) => waiters.push(r))),
    close: () => ws.close(),
  };
}

test('join, relay offer/answer/ice, leave', async () => {
  const a = client();
  const b = client();
  await Promise.all([a.open(), b.open()]);

  a.send({ type: 'join', room: 'r1', name: 'Alice' });
  const ja = await a.next();
  assert.equal(ja.type, 'joined');
  assert.deepEqual(ja.peers, []);

  b.send({ type: 'join', room: 'r1', name: 'Bob' });
  const jb = await b.next();
  assert.deepEqual(jb.peers, [{ id: ja.selfId, name: 'Alice' }]);
  const pj = await a.next();
  assert.equal(pj.type, 'peer-joined');
  assert.equal(pj.peer.name, 'Bob');

  b.send({ type: 'offer', to: ja.selfId, sdp: 'v=0 offer' });
  const off = await a.next();
  assert.deepEqual(off, { type: 'offer', sdp: 'v=0 offer', from: jb.selfId });

  a.send({ type: 'answer', to: jb.selfId, sdp: 'v=0 answer' });
  assert.equal((await b.next()).sdp, 'v=0 answer');

  a.send({ type: 'ice', to: jb.selfId, candidate: null });
  const ice = await b.next();
  assert.equal(ice.type, 'ice');
  assert.equal(ice.candidate, null);

  b.send({ type: 'leave' });
  assert.deepEqual(await a.next(), { type: 'peer-left', id: jb.selfId });
  a.close();
  b.close();
});

test('room cap of 4', async () => {
  const cs = [client(), client(), client(), client(), client()];
  await Promise.all(cs.map((c) => c.open()));
  for (let i = 0; i < 4; i++) {
    cs[i].send({ type: 'join', room: 'full', name: `p${i}` });
    assert.equal((await cs[i].next()).type, 'joined');
  }
  cs[4].send({ type: 'join', room: 'full', name: 'p4' });
  const err = await cs[4].next();
  assert.equal(err.type, 'error');
  assert.equal(err.code, 'ROOM_FULL');
  cs.forEach((c) => c.close());
});

test('rejects bad input and cross-room relay', async () => {
  const a = client();
  const b = client();
  await Promise.all([a.open(), b.open()]);
  a.send('not json');
  assert.equal((await a.next()).code, 'BAD_JSON');
  a.send({ type: 'offer', to: 'x', sdp: 's' });
  assert.equal((await a.next()).code, 'NOT_JOINED');
  a.send({ type: 'join', room: 'bad room!', name: 'A' });
  assert.equal((await a.next()).code, 'BAD_ROOM');

  a.send({ type: 'join', room: 'roomA', name: 'A' });
  await a.next();
  b.send({ type: 'join', room: 'roomB', name: 'B' });
  const jb = await b.next();
  a.send({ type: 'offer', to: jb.selfId, sdp: 's' });
  assert.equal((await a.next()).code, 'UNKNOWN_PEER');

  a.send({ type: 'ping' });
  assert.equal((await a.next()).type, 'pong');
  a.close();
  b.close();
});

test('closing the socket frees the slot and notifies peers', async () => {
  const a = client();
  const b = client();
  await Promise.all([a.open(), b.open()]);
  a.send({ type: 'join', room: 'drop', name: 'A' });
  await a.next();
  b.send({ type: 'join', room: 'drop', name: 'B' });
  const jb = await b.next();
  await a.next(); // peer-joined
  b.ws.terminate();
  assert.deepEqual(await a.next(), { type: 'peer-left', id: jb.selfId });
  a.close();
});
