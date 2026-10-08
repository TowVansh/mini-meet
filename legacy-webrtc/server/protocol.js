// Mini-Meet signalling protocol: message types and validation.
// Full spec lives in docs/PROTOCOL.md. Every message is one JSON object
// sent as a single WebSocket text frame, with a mandatory "type" field.

const PROTOCOL_VERSION = 1;
const MAX_ROOM_SIZE = 4;
const MAX_MESSAGE_BYTES = 64 * 1024;

const ROOM_RE = /^[A-Za-z0-9_-]{1,32}$/;

// Client -> server messages and the fields each one must carry.
const CLIENT_MESSAGES = {
  join: { room: 'string', name: 'string' },
  offer: { to: 'string', sdp: 'string' },
  answer: { to: 'string', sdp: 'string' },
  ice: { to: 'string' }, // candidate may be null (end-of-candidates)
  leave: {},
  ping: {},
};

const ERRORS = {
  BAD_JSON: 'Message is not valid JSON',
  BAD_MESSAGE: 'Unknown type or missing field',
  TOO_LARGE: 'Message exceeds size limit',
  BAD_ROOM: 'Room name must be 1-32 chars of A-Z a-z 0-9 _ -',
  ROOM_FULL: `Room already has ${MAX_ROOM_SIZE} participants`,
  NOT_JOINED: 'Join a room first',
  ALREADY_JOINED: 'Already in a room',
  UNKNOWN_PEER: 'Target peer is not in your room',
};

// Returns { ok: true, msg } or { ok: false, code }.
function parse(raw) {
  if (raw.length > MAX_MESSAGE_BYTES) return { ok: false, code: 'TOO_LARGE' };
  let msg;
  try {
    msg = JSON.parse(raw);
  } catch {
    return { ok: false, code: 'BAD_JSON' };
  }
  if (!msg || typeof msg !== 'object' || Array.isArray(msg)) return { ok: false, code: 'BAD_MESSAGE' };
  const spec = CLIENT_MESSAGES[msg.type];
  if (!spec) return { ok: false, code: 'BAD_MESSAGE' };
  for (const [field, kind] of Object.entries(spec)) {
    if (typeof msg[field] !== kind) return { ok: false, code: 'BAD_MESSAGE' };
  }
  if (msg.type === 'join') {
    if (!ROOM_RE.test(msg.room)) return { ok: false, code: 'BAD_ROOM' };
    msg.name = msg.name.trim().slice(0, 32) || 'Guest';
  }
  return { ok: true, msg };
}

function error(code) {
  return { type: 'error', code, message: ERRORS[code] || code };
}

module.exports = { PROTOCOL_VERSION, MAX_ROOM_SIZE, parse, error, ERRORS };
