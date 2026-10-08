// Per-connection call-quality sampler built on RTCPeerConnection.getStats().
// Every second it turns cumulative counters into per-interval rates and
// returns one row per media stream (audio/video x inbound/outbound).

class StatsSampler {
  constructor(pc) {
    this.pc = pc;
    this.prev = new Map(); // stats id -> previous report
  }

  delta(report, field) {
    const old = this.prev.get(report.id);
    if (!old || report[field] == null || old[field] == null) return null;
    return report[field] - old[field];
  }

  rate(report, bytesField) {
    const old = this.prev.get(report.id);
    const bytes = this.delta(report, bytesField);
    if (bytes == null) return null;
    const secs = (report.timestamp - old.timestamp) / 1000;
    return secs > 0 ? (bytes * 8) / 1000 / secs : null;
  }

  async sample() {
    const reports = await this.pc.getStats();
    const byId = new Map();
    reports.forEach((r) => byId.set(r.id, r));

    // Selected ICE candidate pair: path RTT, bandwidth estimate, NAT result.
    let pair = null;
    reports.forEach((r) => {
      if (r.type === 'transport' && r.selectedCandidatePairId) pair = byId.get(r.selectedCandidatePairId);
    });
    if (!pair) reports.forEach((r) => { if (r.type === 'candidate-pair' && r.nominated && r.state === 'succeeded') pair = r; });
    const local = pair && byId.get(pair.localCandidateId);
    const remote = pair && byId.get(pair.remoteCandidateId);
    const path = {
      rtt_ms: pair?.currentRoundTripTime != null ? pair.currentRoundTripTime * 1000 : null,
      avail_out_kbps: pair?.availableOutgoingBitrate != null ? pair.availableOutgoingBitrate / 1000 : null,
      candidate_type: local ? `${local.candidateType}->${remote?.candidateType}` : null,
      protocol: local?.protocol,
    };

    const rows = [];
    reports.forEach((r) => {
      if (r.type === 'inbound-rtp') {
        const lost = this.delta(r, 'packetsLost');
        const recv = this.delta(r, 'packetsReceived');
        const row = {
          kind: r.kind, direction: 'in',
          bitrate_kbps: this.rate(r, 'bytesReceived'),
          packets_lost: r.packetsLost,
          loss_pct: lost != null && recv != null && lost + recv > 0 ? (100 * Math.max(lost, 0)) / (lost + recv) : null,
          jitter_ms: r.jitter != null ? r.jitter * 1000 : null,
          rtt_ms: path.rtt_ms,
        };
        if (r.kind === 'video') {
          Object.assign(row, { fps: r.framesPerSecond, width: r.frameWidth, height: r.frameHeight, freeze_count: r.freezeCount });
        } else {
          const conc = this.delta(r, 'concealedSamples');
          const total = this.delta(r, 'totalSamplesReceived');
          row.concealed_pct = conc != null && total > 0 ? (100 * conc) / total : null;
        }
        rows.push(row);
      } else if (r.type === 'outbound-rtp') {
        const ri = r.remoteId ? byId.get(r.remoteId) : null; // receiver report about this stream
        const row = {
          kind: r.kind, direction: 'out',
          bitrate_kbps: this.rate(r, 'bytesSent'),
          packets_lost: ri?.packetsLost,
          loss_pct: ri?.fractionLost != null ? ri.fractionLost * 100 : null,
          jitter_ms: ri?.jitter != null ? ri.jitter * 1000 : null,
          rtt_ms: ri?.roundTripTime != null ? ri.roundTripTime * 1000 : path.rtt_ms,
          avail_out_kbps: path.avail_out_kbps,
        };
        if (r.kind === 'video') {
          Object.assign(row, { fps: r.framesPerSecond, width: r.frameWidth, height: r.frameHeight, limit_reason: r.qualityLimitationReason });
        }
        rows.push(row);
      }
    });

    this.prev = byId;
    for (const row of rows) row.candidate_type = path.candidate_type;
    return { rows, path };
  }
}

function fmt(v, digits = 0) {
  return v == null || Number.isNaN(v) ? '–' : Number(v).toFixed(digits);
}

// Compact overlay text for a remote tile.
function overlayText(rows, path, adaptLevel) {
  const vin = rows.find((r) => r.kind === 'video' && r.direction === 'in');
  const ain = rows.find((r) => r.kind === 'audio' && r.direction === 'in');
  const vout = rows.find((r) => r.kind === 'video' && r.direction === 'out');
  const lines = [
    `path  ${path.candidate_type || '…'} ${path.protocol || ''}`,
    `rtt   ${fmt(path.rtt_ms)} ms`,
  ];
  if (vin) lines.push(`v-in  ${fmt(vin.bitrate_kbps)} kbps ${fmt(vin.fps)} fps ${vin.width || '?'}x${vin.height || '?'}`,
    `      loss ${fmt(vin.loss_pct, 1)}% jitter ${fmt(vin.jitter_ms)} ms`);
  if (ain) lines.push(`a-in  ${fmt(ain.bitrate_kbps)} kbps conceal ${fmt(ain.concealed_pct, 1)}%`);
  if (vout) lines.push(`v-out ${fmt(vout.bitrate_kbps)} kbps est ${fmt(vout.avail_out_kbps)} lvl ${adaptLevel}`,
    `      limit ${vout.limit_reason || '–'}`);
  return lines.join('\n');
}
