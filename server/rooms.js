// Room registry. A room is a set of up to MAX_ROOM_SIZE peers; it exists
// while it has at least one member. The server only relays signalling
// between members of the same room and never touches media.

const crypto = require('crypto');
const { MAX_ROOM_SIZE } = require('./protocol');

class Rooms {
  constructor() {
    this.rooms = new Map(); // roomName -> Map(peerId -> peer)
  }

  // peer = { id, name, send(obj) }
  join(roomName, name, send) {
    let room = this.rooms.get(roomName);
    if (!room) {
      room = new Map();
      this.rooms.set(roomName, room);
    }
    if (room.size >= MAX_ROOM_SIZE) return null;
    const peer = { id: crypto.randomBytes(4).toString('hex'), name, room: roomName, send };
    const existing = [...room.values()].map((p) => ({ id: p.id, name: p.name }));
    room.set(peer.id, peer);
    for (const other of room.values()) {
      if (other !== peer) other.send({ type: 'peer-joined', peer: { id: peer.id, name } });
    }
    return { peer, existing };
  }

  leave(peer) {
    const room = this.rooms.get(peer.room);
    if (!room || !room.delete(peer.id)) return;
    for (const other of room.values()) other.send({ type: 'peer-left', id: peer.id });
    if (room.size === 0) this.rooms.delete(peer.room);
  }

  // Forward an offer/answer/ice to one peer in the sender's room.
  relay(from, to, msg) {
    const target = this.rooms.get(from.room)?.get(to);
    if (!target) return false;
    const { to: _drop, ...rest } = msg;
    target.send({ ...rest, from: from.id });
    return true;
  }

  summary() {
    return [...this.rooms].map(([name, room]) => ({
      room: name,
      peers: [...room.values()].map((p) => p.name),
    }));
  }
}

module.exports = { Rooms };
