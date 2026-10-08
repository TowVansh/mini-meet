// Full-mesh media topology: one RTCPeerConnection per remote participant.
// With N people each browser holds N-1 connections; the call has N(N-1)/2
// links in total (6 at the 4-person cap).
//
// Glare avoidance: the newcomer sends offers to everyone already in the
// room; existing members only ever answer. So two peers never offer to each
// other at the same time.

class Mesh {
  constructor({ signalling, localStream, iceServers, adapt, onTrack, onPeerGone, onStats }) {
    this.sig = signalling;
    this.localStream = localStream;
    this.iceServers = iceServers;
    this.adapt = adapt;
    this.onTrack = onTrack;
    this.onPeerGone = onPeerGone;
    this.onStats = onStats;
    this.peers = new Map(); // id -> { pc, name, sampler, adapter, pendingIce }
    this.timer = setInterval(() => this.pollStats(), 1000);

    signalling.on('offer', (m) => this.handleOffer(m));
    signalling.on('answer', (m) => this.handleAnswer(m));
    signalling.on('ice', (m) => this.handleIce(m));
    signalling.on('peer-left', (m) => this.remove(m.id));
  }

  create(id, name) {
    const pc = new RTCPeerConnection({ iceServers: this.iceServers });
    const entry = { pc, name, sampler: new StatsSampler(pc), adapter: null, pendingIce: [] };
    this.peers.set(id, entry);

    for (const track of this.localStream.getTracks()) {
      const sender = pc.addTrack(track, this.localStream);
      if (track.kind === 'video') entry.adapter = new Adapter(sender, this.adapt);
    }
    pc.onicecandidate = (ev) => this.sig.ice(id, ev.candidate ? ev.candidate.toJSON() : null);
    pc.ontrack = (ev) => this.onTrack(id, name, ev.streams[0]);
    pc.onconnectionstatechange = () => {
      if (pc.connectionState === 'failed') console.warn(`connection to ${name} failed (NAT traversal without TURN?)`);
    };
    return entry;
  }

  // Newcomer side: offer to each existing member.
  async call(id, name) {
    const { pc } = this.create(id, name);
    await pc.setLocalDescription(await pc.createOffer());
    this.sig.offer(id, pc.localDescription.sdp);
  }

  async handleOffer({ from, sdp, name }) {
    const entry = this.peers.get(from) || this.create(from, name || this.names?.get(from) || from);
    await entry.pc.setRemoteDescription({ type: 'offer', sdp });
    await this.flushIce(entry);
    await entry.pc.setLocalDescription(await entry.pc.createAnswer());
    this.sig.answer(from, entry.pc.localDescription.sdp);
  }

  async handleAnswer({ from, sdp }) {
    const entry = this.peers.get(from);
    if (!entry) return;
    await entry.pc.setRemoteDescription({ type: 'answer', sdp });
    await this.flushIce(entry);
  }

  // Candidates can arrive before the remote description; queue them.
  async handleIce({ from, candidate }) {
    const entry = this.peers.get(from);
    if (!entry) return;
    if (!entry.pc.remoteDescription) {
      entry.pendingIce.push(candidate);
      return;
    }
    await this.addIce(entry.pc, candidate);
  }

  async flushIce(entry) {
    for (const c of entry.pendingIce.splice(0)) await this.addIce(entry.pc, c);
  }

  async addIce(pc, candidate) {
    try {
      await pc.addIceCandidate(candidate || null);
    } catch (err) {
      console.warn('addIceCandidate', err);
    }
  }

  remove(id) {
    const entry = this.peers.get(id);
    if (!entry) return;
    entry.pc.close();
    this.peers.delete(id);
    this.onPeerGone(id);
  }

  async pollStats() {
    for (const [id, entry] of this.peers) {
      if (entry.pc.connectionState === 'closed') continue;
      try {
        const { rows, path } = await entry.sampler.sample();
        const vout = rows.find((r) => r.kind === 'video' && r.direction === 'out');
        if (entry.adapter && vout) {
          await entry.adapter.update(
            vout.loss_pct != null ? vout.loss_pct / 100 : null,
            vout.rtt_ms != null ? vout.rtt_ms / 1000 : null,
          );
        }
        const level = entry.adapter ? entry.adapter.level : 0;
        for (const r of rows) {
          r.remote = entry.name;
          r.adapt_on = this.adapt ? 1 : 0;
          r.adapt_level = level;
        }
        this.onStats(id, rows, path, level);
      } catch (err) {
        console.warn('stats', err);
      }
    }
  }

  close() {
    clearInterval(this.timer);
    for (const id of [...this.peers.keys()]) this.remove(id);
  }
}
