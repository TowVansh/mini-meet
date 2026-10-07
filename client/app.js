// UI glue: lobby -> local media -> signalling -> mesh -> tiles.
// URL parameters (used by bench/bot.js, also handy for demos):
//   ?room=demo&name=Alice&auto=1&adapt=0&run=loss5-adapt0

const $ = (sel) => document.querySelector(sel);
const params = new URLSearchParams(location.search);

const state = {
  sig: null,
  mesh: null,
  localStream: null,
  run: null,
  selfName: '',
  statBuffer: [],
  tiles: new Map(), // peerId|'self' -> tile element
};
window.miniMeet = state; // exposed for debugging and the benchmark bot

$('#name').value = params.get('name') || localStorage.getItem('mm-name') || '';
$('#room').value = params.get('room') || '';
if (params.has('adapt')) $('#adapt').checked = params.get('adapt') !== '0';
if (params.has('run')) $('#record').checked = true;

$('#join-form').addEventListener('submit', (ev) => {
  ev.preventDefault();
  join().catch((err) => {
    $('#lobby-error').textContent = err.message;
    console.error(err);
  });
});
if (params.get('auto') === '1') join().catch((err) => console.error(err));

function setStatus(text) {
  $('#status').textContent = text;
}

function addTile(id, name, stream, self = false) {
  let tile = state.tiles.get(id);
  if (!tile) {
    tile = $('#tile-tpl').content.firstElementChild.cloneNode(true);
    if (self) tile.classList.add('self');
    $('#grid').appendChild(tile);
    state.tiles.set(id, tile);
  }
  tile.querySelector('.name').textContent = self ? `${name} (you)` : name;
  const video = tile.querySelector('video');
  if (video.srcObject !== stream) video.srcObject = stream;
  video.muted = self;
  layout();
}

function removeTile(id) {
  state.tiles.get(id)?.remove();
  state.tiles.delete(id);
  layout();
}

function layout() {
  $('#grid').className = `n${state.tiles.size}`;
  setStatus(`${state.tiles.size} in call`);
}

async function join() {
  const name = $('#name').value.trim() || 'Guest';
  const room = $('#room').value.trim();
  if (!room) throw new Error('Enter a room name');
  localStorage.setItem('mm-name', name);
  state.selfName = name;
  const adapt = $('#adapt').checked;
  if ($('#record').checked) state.run = params.get('run') || `live-${room}-${Date.now()}`;

  const config = await (await fetch('/api/config')).json();
  state.localStream = await navigator.mediaDevices.getUserMedia({
    audio: true,
    video: { width: { ideal: 1280 }, height: { ideal: 720 }, frameRate: { ideal: 30 } },
  });

  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
  const sig = new Signalling(`${proto}//${location.host}/ws`);
  state.sig = sig;
  await sig.connect();

  const names = new Map();
  sig.on('error', (m) => {
    $('#lobby-error').textContent = m.message;
    setStatus(`error: ${m.code}`);
  });
  sig.on('close', () => setStatus('signalling disconnected (media keeps flowing peer-to-peer)'));

  sig.on('joined', async (m) => {
    $('#lobby').hidden = true;
    $('#call').hidden = false;
    $('#room-label').textContent = `Room: ${m.room}`;
    addTile('self', name, state.localStream, true);

    state.mesh = new Mesh({
      signalling: sig,
      localStream: state.localStream,
      iceServers: config.iceServers,
      adapt,
      onTrack: (id, peerName, stream) => addTile(id, names.get(id) || peerName, stream),
      onPeerGone: removeTile,
      onStats: (id, rows, path, level) => {
        const tile = state.tiles.get(id);
        if (tile) tile.querySelector('.stats').textContent = overlayText(rows, path, level);
        if (state.run) {
          const ts = Date.now();
          for (const r of rows) state.statBuffer.push({ ts, run: state.run, peer: name, ...r });
        }
      },
    });
    state.mesh.names = names;
    for (const p of m.peers) {
      names.set(p.id, p.name);
      await state.mesh.call(p.id, p.name);
    }
  });

  sig.on('peer-joined', (m) => names.set(m.peer.id, m.peer.name));

  sig.join(room, name);
}

// Ship recorded stats to the server every 5 s.
setInterval(flushStats, 5000);
function flushStats() {
  if (!state.run || state.statBuffer.length === 0) return;
  const rows = state.statBuffer.splice(0);
  fetch('/api/stats', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ run: state.run, rows }),
    keepalive: true,
  }).catch(() => {});
}

$('#btn-mic').onclick = () => {
  const track = state.localStream.getAudioTracks()[0];
  track.enabled = !track.enabled;
  $('#btn-mic').textContent = track.enabled ? 'Mute' : 'Unmute';
};
$('#btn-cam').onclick = () => {
  const track = state.localStream.getVideoTracks()[0];
  track.enabled = !track.enabled;
  $('#btn-cam').textContent = track.enabled ? 'Camera off' : 'Camera on';
};
$('#btn-stats').onclick = () => {
  document.body.classList.toggle('hide-stats');
  $('#btn-stats').textContent = document.body.classList.contains('hide-stats') ? 'Show stats' : 'Hide stats';
};
$('#btn-leave').onclick = leave;
window.addEventListener('beforeunload', leave);

function leave() {
  flushStats();
  state.mesh?.close();
  state.sig?.leave();
  state.localStream?.getTracks().forEach((t) => t.stop());
  for (const id of [...state.tiles.keys()]) removeTile(id);
  $('#call').hidden = true;
  $('#lobby').hidden = false;
}
