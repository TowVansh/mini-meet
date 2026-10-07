// Thin client for the Mini-Meet signalling protocol (docs/PROTOCOL.md).
// Emits one event per server message type: on('joined', fn), on('offer', fn) ...

class Signalling {
  constructor(url) {
    this.url = url;
    this.handlers = {};
    this.ws = null;
  }

  on(type, fn) {
    (this.handlers[type] ||= []).push(fn);
  }

  emit(type, msg) {
    for (const fn of this.handlers[type] || []) fn(msg);
  }

  connect() {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(this.url);
      this.ws = ws;
      ws.onopen = () => resolve();
      ws.onerror = () => reject(new Error('Cannot reach signalling server'));
      ws.onclose = () => this.emit('close', {});
      ws.onmessage = (ev) => {
        let msg;
        try { msg = JSON.parse(ev.data); } catch { return; }
        this.emit(msg.type, msg);
      };
    });
  }

  send(obj) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify(obj));
  }

  join(room, name) { this.send({ type: 'join', room, name }); }
  offer(to, sdp) { this.send({ type: 'offer', to, sdp }); }
  answer(to, sdp) { this.send({ type: 'answer', to, sdp }); }
  ice(to, candidate) { this.send({ type: 'ice', to, candidate }); }

  leave() {
    this.send({ type: 'leave' });
    this.ws?.close();
  }
}
