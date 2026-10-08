// Application-level quality adaptation, layered on top of WebRTC's built-in
// congestion control (Google Congestion Control).
//
// Each outgoing video sender walks a ladder of (max bitrate, downscale)
// levels using an AIMD rule, the same idea as TCP congestion control:
//   - congestion seen (receiver-reported loss or RTT over threshold)
//       -> multiplicative decrease: jump DOWN two levels at once
//   - CLEAN_ROUNDS consecutive clean intervals
//       -> additive increase: step UP one level
// The receiver's view of loss/RTT arrives in RTCP receiver reports, which
// the browser exposes as "remote-inbound-rtp" stats.

const ADAPT_LADDER = [
  { maxBitrate: 1500_000, scale: 1 },
  { maxBitrate: 900_000, scale: 1 },
  { maxBitrate: 600_000, scale: 1.5 },
  { maxBitrate: 350_000, scale: 2 },
  { maxBitrate: 200_000, scale: 3 },
  { maxBitrate: 120_000, scale: 4 },
];

const ADAPT_LOSS_BAD = 0.08;  // 8 % loss
const ADAPT_LOSS_OK = 0.02;
const ADAPT_RTT_BAD = 0.4;    // seconds
const ADAPT_RTT_OK = 0.25;
const CLEAN_ROUNDS = 3;

class Adapter {
  constructor(sender, enabled) {
    this.sender = sender;
    this.enabled = enabled;
    this.level = 0;
    this.clean = 0;
  }

  // Called once per stats interval with receiver-reported loss (0..1) and RTT (s).
  async update(loss, rtt) {
    if (!this.enabled || loss == null) return;
    const bad = loss > ADAPT_LOSS_BAD || (rtt != null && rtt > ADAPT_RTT_BAD);
    const good = loss < ADAPT_LOSS_OK && (rtt == null || rtt < ADAPT_RTT_OK);
    let next = this.level;
    if (bad) {
      next = Math.min(this.level + 2, ADAPT_LADDER.length - 1);
      this.clean = 0;
    } else if (good) {
      this.clean += 1;
      if (this.clean >= CLEAN_ROUNDS) {
        next = Math.max(this.level - 1, 0);
        this.clean = 0;
      }
    } else {
      this.clean = 0;
    }
    if (next !== this.level) await this.apply(next);
  }

  async apply(level) {
    const params = this.sender.getParameters();
    if (!params.encodings || params.encodings.length === 0) params.encodings = [{}];
    const step = ADAPT_LADDER[level];
    params.encodings[0].maxBitrate = step.maxBitrate;
    params.encodings[0].scaleResolutionDownBy = step.scale;
    try {
      await this.sender.setParameters(params);
      this.level = level;
    } catch (err) {
      console.warn('setParameters failed', err);
    }
  }
}
