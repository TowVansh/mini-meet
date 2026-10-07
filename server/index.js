// Mini-Meet server: serves the web client, runs the WebSocket signalling
// endpoint (/ws) and collects call-quality stats (/api/stats).
//
//   PORT=8443 node server/index.js
//
// If certs/key.pem and certs/cert.pem exist it serves HTTPS/WSS (needed for
// camera access from other machines on a LAN). On localhost, or behind a
// TLS tunnel such as cloudflared, plain HTTP is fine.

const fs = require('fs');
const http = require('http');
const https = require('https');
const path = require('path');
const express = require('express');
const { WebSocketServer } = require('ws');

const { PROTOCOL_VERSION, parse, error } = require('./protocol');
const { Rooms } = require('./rooms');
const stats = require('./stats');

const PORT = Number(process.env.PORT) || 8443;
const HEARTBEAT_MS = 15000;
const STUN_URLS = (process.env.STUN_URLS || 'stun:stun.l.google.com:19302,stun:stun1.l.google.com:19302')
  .split(',').map((s) => s.trim()).filter(Boolean);

const app = express();
app.use(express.json({ limit: '1mb' }));
app.use(express.static(path.join(__dirname, '..', 'client')));

app.get('/api/config', (_req, res) => {
  res.json({ protocol: PROTOCOL_VERSION, iceServers: [{ urls: STUN_URLS }] });
});

app.post('/api/stats', (req, res) => {
  const ok = stats.append(String(req.body?.run || ''), req.body?.rows);
  res.status(ok ? 204 : 400).end();
});

const certDir = path.join(__dirname, '..', 'certs');
const useTls = fs.existsSync(path.join(certDir, 'key.pem')) && fs.existsSync(path.join(certDir, 'cert.pem'));
const server = useTls
  ? https.createServer({ key: fs.readFileSync(path.join(certDir, 'key.pem')), cert: fs.readFileSync(path.join(certDir, 'cert.pem')) }, app)
  : http.createServer(app);

const rooms = new Rooms();
const wss = new WebSocketServer({ server, path: '/ws' });

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

wss.on('connection', (ws, req) => {
  let peer = null;
  ws.isAlive = true;
  const send = (obj) => {
    if (ws.readyState === ws.OPEN) ws.send(JSON.stringify(obj));
  };

  ws.on('pong', () => { ws.isAlive = true; });

  ws.on('message', (data) => {
    const parsed = parse(data.toString());
    if (!parsed.ok) return send(error(parsed.code));
    const msg = parsed.msg;

    switch (msg.type) {
      case 'ping':
        return send({ type: 'pong', t: Date.now() });

      case 'join': {
        if (peer) return send(error('ALREADY_JOINED'));
        const result = rooms.join(msg.room, msg.name, send);
        if (!result) return send(error('ROOM_FULL'));
        peer = result.peer;
        log(`join  room=${msg.room} peer=${peer.id} name=${peer.name} from=${req.socket.remoteAddress}`);
        return send({ type: 'joined', protocol: PROTOCOL_VERSION, selfId: peer.id, room: msg.room, peers: result.existing });
      }

      case 'offer':
      case 'answer':
      case 'ice':
        if (!peer) return send(error('NOT_JOINED'));
        if (!rooms.relay(peer, msg.to, msg)) send(error('UNKNOWN_PEER'));
        return;

      case 'leave':
        if (peer) {
          log(`leave room=${peer.room} peer=${peer.id}`);
          rooms.leave(peer);
          peer = null;
        }
        return;
    }
  });

  ws.on('close', () => {
    if (peer) {
      log(`close room=${peer.room} peer=${peer.id}`);
      rooms.leave(peer);
      peer = null;
    }
  });
});

// Drop sockets that stop answering WebSocket pings (closed laptop lid,
// lost network) so their room slot is freed and others get peer-left.
const heartbeat = setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.isAlive) {
      ws.terminate();
      continue;
    }
    ws.isAlive = false;
    ws.ping();
  }
}, HEARTBEAT_MS);
wss.on('close', () => clearInterval(heartbeat));

app.get('/api/rooms', (_req, res) => res.json(rooms.summary()));

if (require.main === module) {
  server.listen(PORT, () => {
    log(`Mini-Meet on ${useTls ? 'https' : 'http'}://localhost:${PORT}  STUN=${STUN_URLS.join(' ')}`);
  });
}

module.exports = { server, wss, rooms };
