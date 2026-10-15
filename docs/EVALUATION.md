# Call-Quality Evaluation Under Network Impairment (C client)

## Method

- **Testbed** (`bench/run-matrix.sh`): WSL2 Ubuntu 24.04. Two Linux **network namespaces** (`mmA` 10.10.0.1, `mmB` 10.10.0.2) are joined by a veth pair, so they behave like two separate hosts. `mm-server` and client botA run in `mmA`, botB runs in `mmB`.
- **Impairment**: `tc netem` on **both** veth ends (`scripts/impair.sh`), so each direction is impaired separately, like a real access link. A "1 Mbit/s" profile is 1 Mbit/s each way, and `delay 100ms` gives RTT ≈ 200 ms.
- **Media**: the built-in test pattern (VP8, up to 640×480 at 20 fps) and a test tone (Opus 32 kbps). Same input in every run. End-to-end encryption on (ChaCha20-Poly1305, +16 bytes per packet).
- **Procedure**: start the call → 10 s clean → apply the profile **mid-call** → record 45 s → clear. 14 profiles × {adaptation off (`--no-adapt`, fixed 1200 kbps), on (AIMD + slow start + feedback-starvation detection)} = 28 runs.
- **Metrics**: per-second CSV from each client. The table shows steady state only (t ≥ 15 s): medians for bitrate, fps, height, jitter and RTT; means for loss and audio concealment. Freezes are summed over both receivers. "target" is the controller's bitrate.
- **Reproduce**: `wsl -d Ubuntu -u root -- bash bench/run-matrix.sh`, then `python3 analysis/plot.py results/eval_*.csv`.

| Profile | netem (per direction) |
|---|---|
| baseline | none |
| loss1 / 5 / 10 / 20 | `loss N%` random |
| burst5 | `loss gemodel 1% 30% 70% 0.1%` (bursty, Gilbert-Elliott) |
| delay100 | `delay 100ms` |
| jitter30 / jitter60 | `delay 100ms ±30/60ms` normal distribution (also reorders packets) |
| rate2m / 1m / 500k / 250k | `rate N`, netem's default 1000-packet FIFO |
| combo | `delay 80ms ±20ms, loss 3%, rate 1mbit` |

## Results

| profile | adapt | video kbps | fps | height | video loss % | jitter ms | RTT ms | freezes | audio conceal % | target kbps |
|---|---|---|---|---|---|---|---|---|---|---|
| baseline | off | 1222 | 20 | 480 | 0.0 | 0.1 | 0.2 | 0 | 0.0 | 1200 |
| baseline | on | 1222 | 20 | 480 | 0.0 | 0.1 | 0.3 | 0 | 0.0 | 1200 |
| loss1 | off | 1209 | 20 | 480 | 0.9 | 0.1 | 0.2 | 0 | 0.0 | 1200 |
| loss1 | on | 1231 | 20 | 480 | 0.9 | 0.1 | 3.1 | 0 | 0.0 | 1200 |
| loss5 | off | 1232 | 20 | 480 | 4.8 | 0.1 | 0.3 | 0 | 0.3 | 1200 |
| loss5 | on | 194 | 20 | 240 | 3.9 | 0.2 | 0.3 | 0 | 0.3 | 180 |
| loss10 | off | 1228 | 20 | 480 | 9.8 | 0.1 | 0.1 | 2 | 1.0 | 1200 |
| loss10 | on | 105 | 20 | 120 | 9.2 | 0.1 | 0.4 | 1 | 1.0 | 100 |
| loss20 | off | 1201 | 15.5 | 480 | 20.0 | 0.2 | 0.2 | **39** | 8.6 | 1200 |
| loss20 | on | 105 | 20 | 120 | 19.7 | 0.2 | 4.0 | **15** | 4.2 | 100 |
| burst5 | off | 1232 | 20 | 480 | 2.1 | 0.1 | 0.2 | 0 | 0.5 | 1200 |
| burst5 | on | 1229 | 20 | 480 | 1.8 | 0.1 | 0.4 | 0 | 0.2 | 1200 |
| delay100 | off | 1220 | 20 | 480 | 0.0 | 0.1 | 200 | 0 | 0.0 | 1200 |
| delay100 | on | 526 | 20 | 360 | 0.0 | 0.1 | 201 | 0 | 0.0 | 500 |
| jitter30 | off | 1399 | 20 | 480 | 1.1 | 33.6 | 210 | 1 | 4.5 | 1200 |
| jitter30 | on | 341 | 20 | 240 | 0.6 | 34.3 | 203 | 0 | 0.0 | 300 |
| jitter60 | off | 1666 | 20 | 480 | 1.3 | 64.2 | 228 | 5 | 4.5 | 1200 |
| jitter60 | on | 119 | 20 | 120 | 1.5 | 67.3 | 205 | 29 | 0.5 | 100 |
| rate2m | off | 1231 | 20 | 480 | 0.0 | 7.7 | 37 | 0 | 0.0 | 1200 |
| rate2m | on | 1232 | 20 | 480 | 0.0 | 7.2 | 39 | 0 | 0.0 | 1200 |
| **rate1m** | off | 923 | 14 | 480 | 14.3 | 13.7 | **10756** | 0 | **30.9** | 1200 |
| **rate1m** | on | 198 | **20** | 240 | **0.0** | 4.3 | **2.9** | 0 | **1.9** | 180 |
| **rate500k** | off | 446 | **0** | 480 | 40.2 | 23.4 | **18965** | 0 | **43.1** | 1200 |
| **rate500k** | on | 188 | **20** | 120 | **0.0** | 3.4 | **15.5** | 0 | 9.5 | 100 |
| rate250k | off | 229 | 4 | 480 | 16.5 | 31.5 | 13192 | 2 | 80.1 | 1200 |
| rate250k | on | 165 | 4 | 120 | 1.5 | 19.3 | 11647 | 6 | 31.9 | 100 |
| **combo** | off | 925 | **0** | 480 | 14.3 | 13.7 | **10062** | 8 | 31.2 | 1200 |
| **combo** | on | 109 | **20** | 120 | 3.3 | 17.2 | **189** | 58 | 0.1 | 100 |

Graphs in `results/`: `bars_*.png` (each metric, adaptation off vs on) and `timeline_<profile>.png` (send rate vs target, fps, loss and RTT over time). `timeline_rate1m.png` shows the AIMD sawtooth.

**4-peer mesh** (`results/mesh4_baseline_adapt1_p4__*.csv`, Windows): all **12 directed video streams** received at 640×480, 20 fps, about 1140–1200 kbps each. Each client uploads 3 copies (~3.6 Mbit/s), which is the cost of mesh.

**Real cross-network call** (NAT_DEMO.md): college Wi-Fi ↔ laptop on a mobile hotspot, `srflx->srflx`, median RTT 85 ms with spikes to 1.15 s from the hotspot's uplink buffer. The controller followed the spikes down and back up.

## How the system degrades

1. **Random loss up to 5 % is invisible.** The video stays at 640×480/20 fps with no freezes: NACK retransmission recovers lost packets well before the frame deadline. Audio concealment stays at 0.3 %, because Opus FEC rebuilds lost frames from the next packet.
2. **At 10–20 % loss, video breaks down.** Some frames lose a packet *and* its retransmission. The frame is skipped, later frames reference it, and the receiver waits for a keyframe (PLI): a freeze. At 20 % loss there are 39 freezes without adaptation. Audio holds up better thanks to FEC.
3. **Constant delay costs nothing.** At 200 ms RTT, quality is identical to baseline.
4. **Jitter reorders packets.** At ±60 ms frames arrive late or out of order (5 freezes). The received bitrate goes *above* the send rate (1666 vs 1200 kbps), because heavy reordering still triggers some unnecessary retransmissions even with the reorder wait.
5. **Bandwidth caps without rate control are catastrophic: bufferbloat.** 1200 kbps into a 1 Mbit/s link fills netem's 1000-packet FIFO. RTT climbs steadily to **10.8 s**, then the full queue drops packets (14–40 %), video stops (0 fps at 500 k) and audio breaks up (31–80 % concealed).

## Effect of the rate controller

| Situation | Result |
|---|---|
| **1 Mbit/s, 500 kbit/s caps, combo** | **Decisive win.** RTT stays at **3–16 ms instead of 10–19 s**, loss is 0 %, video runs at 20 fps instead of freezing, audio is clean. In combo the fixed-rate call stalls completely (0 fps, RTT 10 s), while the adaptive one keeps running at 20 fps and 189 ms. |
| Clean, 1 % loss, bursty loss, 2 Mbit/s | No change: it stays at the top level, as designed. |
| Fixed +100 ms delay | Short dip, then recovery: the 10 s windowed minimum learns the new path delay. (The first version without the window stayed at the bottom level forever; this test caught that bug.) |
| 20 % random loss | Fewer freezes (39 → 15). Smaller frames have fewer packets that can be lost. |
| 5–10 % random loss | **Over-reacts.** It cannot tell random (wireless) loss from congestion loss, so it drops to 100–200 kbps although NACK was coping. This is the classic weakness of loss-based control on wireless links. |
| Jitter ±30/±60 ms | **Over-reacts.** Random delay variation looks like queuing delay. At ±60 ms it made freezes worse (29 vs 5). |
| 250 kbit/s cap (sudden drop mid-call) | **Reacts fast but cannot drain the queue in time.** Feedback-starvation detection backs off within 2.5 s and reaches the lowest rung within 4 s. Before this fix, the controller waited for the stuck reports (4–13 s). But the 2.5 s burst at 1200 kbps already left several megabits in netem's 1000-packet queue. Even the lowest rung plus audio (~180 kbps on the wire) leaves only ~70 kbps to drain it, so RTT stays high for the whole run. Audio still improved (80 % → 32 % concealed). In a separate run, the same profile reached 20 fps after the queue drained, so the outcome depends on how much queued before the reaction. The fix would be an emergency "pause video" rung, or AQM (CoDel) in the network. |

**Conclusion.** Rate control is essential for real-time media over UDP. Without it, any bottleneck slower than the sending rate turns into seconds of delay and then massive loss. Our controller, a simple AIMD with a delay signal, slow start and feedback-starvation detection, keeps calls usable down to about 500 kbit/s. Its remaining weaknesses are textbook ones: random loss and jitter are misread as congestion, and a huge network buffer (bufferbloat) cannot be emptied by the sender alone.

## Comparison with the WebRTC prototype (`results/v1/`, `legacy-webrtc/EVALUATION-webrtc.md`)

| | WebRTC (Chrome GCC) | Mini-Meet C (AIMD) |
|---|---|---|
| Random loss ≤ 5 % | unaffected | unaffected (adaptation off); over-reacts (on) |
| 1 Mbit/s cap | GCC settled very low (~50 kbps), RTT small | ~200 kbps at 20 fps, RTT 3 ms |
| 250 kbit/s cap | RTT ~11 s | RTT ~12 s (both stuck behind the same netem buffer) |
| Visibility | browser black box | every mechanism in ~3,700 lines of our C |

*v1 ran on a single shared `lo` queue and the C runs on per-direction veth links, so the comparison is indicative only.*

## Threats to validity

- Synthetic video: the test pattern's complexity is fixed, while real camera content varies.
- One 45 s run per cell. Small differences (0 vs 1 freeze) are not significant, and the rate250k case varies between runs.
- netem `rate` uses a 1000-packet FIFO, larger than most router queues (but similar to some mobile uplinks, as the NAT demo showed).
- Freeze counts depend on our jitter-buffer wait limits.
