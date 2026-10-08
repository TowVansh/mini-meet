# Call-Quality Evaluation Under Network Impairment (C client)

## Method

- **Testbed** (`bench/run-matrix.sh`): WSL2 Ubuntu 24.04. Two Linux **network namespaces** (`mmA` 10.10.0.1, `mmB` 10.10.0.2) are joined by a veth pair, so they behave like two separate hosts. `mm-server` and client botA run in `mmA`, botB runs in `mmB`.
- **Impairment**: `tc netem` on **both** veth ends (`scripts/impair.sh`), so each direction is impaired separately, like a real access link. A "1 Mbit/s" profile means 1 Mbit/s each way, and `delay 100ms` gives RTT ≈ 200 ms.
- **Media**: the built-in test pattern (moving gradient plus a noise box: VP8 at 640×480, 20 fps) and a test tone (Opus 32 kbps). Identical input in every run.
- **Procedure**: 10 s clean call → apply profile → 45 s recorded → clear. 14 profiles × {adaptation off (`--no-adapt`, fixed 1200 kbps), on (AIMD)} = 28 runs.
- **Metrics**: per-second CSV from each client (`netloop.c:stats_tick`). The table shows steady state only (t ≥ 15 s): medians for bitrate, fps, height, jitter and RTT; means for loss and audio concealment. Freezes are summed over both receivers. "target" is the controller's bitrate.
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
| baseline | off | 1209 | 20 | 480 | 0.0 | 0.1 | 0.2 | 0 | 0.0 | 1200 |
| baseline | on | 1214 | 20 | 480 | 0.0 | 0.0 | 0.1 | 0 | 0.0 | 1200 |
| loss1 | off | 1204 | 20 | 480 | 1.2 | 0.1 | 4.0 | 0 | 0.0 | 1200 |
| loss1 | on | 1214 | 20 | 480 | 1.0 | 0.1 | 0.2 | 0 | 0.0 | 1200 |
| loss5 | off | 1214 | 20 | 480 | 5.3 | 0.0 | 0.2 | 0 | 0.3 | 1200 |
| loss5 | on | 196 | 20 | 240 | 4.9 | 0.1 | 0.1 | 1 | 0.3 | 180 |
| loss10 | off | 1214 | 20 | 480 | 9.7 | 0.1 | 0.2 | 3 | 1.2 | 1200 |
| loss10 | on | 103 | 20 | 120 | 11.5 | 0.2 | 0.2 | 3 | 1.0 | 100 |
| loss20 | off | 1215 | 19 | 480 | 19.5 | 0.1 | 0.2 | 31 | 3.5 | 1200 |
| loss20 | on | 101 | 20 | 120 | 18.3 | 0.2 | 0.2 | 12 | 3.4 | 100 |
| burst5 | off | 1215 | 20 | 480 | 2.4 | 0.0 | 0.2 | 0 | 0.7 | 1200 |
| burst5 | on | 1197 | 20 | 480 | 2.2 | 0.1 | 2.1 | 0 | 0.7 | 1200 |
| delay100 | off | 1211 | 20 | 480 | 0.0 | 0.1 | 201 | 0 | 0.0 | 1200 |
| delay100 | on | 607 | 20 | 420 | 0.0 | 0.2 | 201 | 0 | 0.0 | 650 |
| jitter30 | off | 1337 | 20 | 480 | 0.9 | 32.5 | 189 | 0 | 0.6 | 1200 |
| jitter30 | on | 203 | 20 | 240 | 0.8 | 35.4 | 203 | 0 | 0.4 | 180 |
| jitter60 | off | 1624 | 20 | 480 | 1.2 | 64.7 | 167 | 13 | 0.0 | 1200 |
| jitter60 | on | 112 | 20 | 120 | 1.5 | 68.2 | 179 | 39 | 7.0 | 100 |
| rate2m | off | 1215 | 20 | 480 | 0.0 | 7.9 | 46 | 0 | 0.0 | 1200 |
| rate2m | on | 1219 | 20 | 480 | 0.0 | 7.8 | 33 | 1 | 0.0 | 1200 |
| **rate1m** | off | 927 | 15 | 480 | 13.2 | 13.6 | **10439** | 0 | **29.2** | 1200 |
| **rate1m** | on | 280 | **20** | 240 | **0.0** | 4.5 | **2.5** | 0 | 3.3 | 300 |
| **rate500k** | off | 451 | **0** | 480 | 41.4 | 23.1 | **18431** | 1 | **43.4** | 1200 |
| **rate500k** | on | 256 | **20** | 240 | **0.0** | 8.6 | **23** | 0 | 7.2 | 180 |
| rate250k | off | 230 | 4 | 480 | 17.2 | 31.3 | 13030 | 2 | 77.2 | 1200 |
| rate250k | on | 229 | 4 | 480 | 7.3 | 31.2 | 12842 | 2 | 72.2 | 100 |
| **combo** | off | 925 | **0** | 480 | 13.2 | 13.6 | **9740** | 10 | 30.6 | 1200 |
| **combo** | on | 143 | **20** | 120 | 2.7 | 15.2 | **188** | 54 | 1.3 | 100 |

Graphs in `results/`: `bars_*.png` (each metric, adaptation off vs on) and `timeline_<profile>.png` (send rate vs target, fps, loss and RTT over time). `timeline_rate1m.png` shows the AIMD sawtooth clearly.

**4-peer mesh** (`results/mesh4_baseline_adapt1_p4__*.csv`, Windows, 4 clients): all **12 directed video streams** were received at 640×480, 20 fps, about 1140–1200 kbps each. Each client sends 3 copies of its stream (~3.6 Mbit/s up), which is the uplink cost of mesh.

**Real cross-network call** (NAT_DEMO.md): college Wi-Fi ↔ laptop on a mobile hotspot, `srflx->srflx`, median RTT 85 ms with spikes to 1.15 s from the hotspot's uplink buffer. The controller followed the spikes down and back up.

## How the system degrades

1. **Random loss up to 5 % is invisible.** The video stays at 640×480/20 fps with no freezes. NACK retransmission recovers lost packets well inside the frame deadline, because the RTT is short. Audio concealment stays at 0.3 %: Opus FEC rebuilds most lost frames from the next packet, so PLC is rarely needed.
2. **At 10–20 % random loss, video breaks down.** Some frames lose a packet *and* its retransmission. The frame is skipped, later frames reference it, and the receiver must wait for a keyframe (PLI), which shows up as a freeze. Freezes rise from 3 (10 %) to 31 (20 %, no adaptation). Audio holds up better (3.5 % concealed at 20 % loss) thanks to FEC.
3. **Constant delay costs nothing.** At 200 ms RTT, quality is identical to baseline without adaptation.
4. **Jitter reorders packets and fills the jitter buffer.** With ±60 ms, frames arrive late or out of order: 13 freezes without adaptation. Received bitrate goes *above* the send rate (1624 kbps vs 1200 kbps), because heavy reordering still triggers retransmissions of packets that weren't actually lost.
5. **Bandwidth caps without rate control are catastrophic: bufferbloat.** At 1200 kbps into a 1 Mbit/s link, netem's 1000-packet FIFO fills up. RTT climbs steadily to **10 s** (see `timeline_rate1m.png`). Once the queue is full, packets are dropped (13–41 %), video stops (0 fps at 500 k) and audio is mostly concealed (29–77 %). Nothing in the system slows down, because the sender keeps sending at a fixed rate.

## Effect of the AIMD controller

| Situation | Result |
|---|---|
| **Bandwidth caps (1 Mbit/s, 500 kbit/s) and combo** | **Decisive win.** The controller notices the queue building (queuing delay > 120 ms) within about a second and cuts to 300/180 kbps. RTT stays at **2.5–23 ms instead of 10–18 s**, loss drops to 0 %, video stays at 20 fps and audio is almost clean. In combo, video keeps running (20 fps) where the fixed-rate sender stalls completely (0 fps, RTT 9.7 s). |
| Clean, 1 % loss, bursty loss, 2 Mbit/s | No change: it stays at the top level, as designed. |
| Fixed +100 ms delay | Short dip, then recovery. The jump in RTT looks like a queue at first. Once the 10 s windowed minimum moves past the old samples, the new delay becomes the baseline and the controller climbs back. (A version without the window stayed at the bottom level forever. This test found that bug.) |
| Random loss 5–20 % | **Over-reacts.** It cannot tell random (wireless) loss from congestion loss, so it drops to 100–200 kbps even though the link has spare capacity and NACK was coping. It did reduce freezes at 20 % loss (31 → 12), because smaller frames have fewer packets that can be lost. This is the classic weakness of loss-based congestion control on wireless links, and why modern algorithms (BBR, GCC) rely more on delay. |
| Jitter ±30/±60 ms | **Over-reacts.** Random delay variation looks like queuing delay. At ±60 ms it made things worse (39 freezes vs 13). |
| 250 kbit/s cap | **Too slow to save it.** The fixed starting rate (1200 kbps) fills the 1000-packet queue faster than feedback can come back, because the RTCP reports wait in the same queue. By the time the controller reacts, the queue holds tens of seconds of data, and it can't drain within the 45 s run. Starting at a low rate and ramping up (like TCP slow start, or WebRTC's 300 kbps start) would fix this. |

**Conclusion.** Rate control is not optional for real-time media over UDP. Without it, any bottleneck slower than the sending rate turns into seconds of delay and then massive loss. A simple AIMD controller with a delay signal keeps the call usable down to about 500 kbit/s. Its weaknesses are the same as those of classic TCP-style control: it misreads random loss and jitter as congestion, and it reacts slowly when the feedback itself is stuck behind a full queue.

## Comparison with the WebRTC prototype (`results/v1/`, `legacy-webrtc/EVALUATION-webrtc.md`)

| | WebRTC (Chrome GCC) | Mini-Meet C (AIMD) |
|---|---|---|
| Random loss ≤ 5 % | unaffected | unaffected (adaptation off); over-reacts (on) |
| 1 Mbit/s cap | GCC settled very low (~50 kbps); RTT stayed small | settles ~300 kbps, RTT 2.5 ms, 20 fps |
| Bufferbloat at 250 kbit/s | RTT ~11 s | RTT ~13 s (both fail: the queue fills before feedback returns) |
| Code size | browser does it all | everything visible and modifiable, ~3,000 lines of C |

*Note: v1 ran on a single `lo` interface (both directions shared one queue), and the C run used per-direction veth links, so the numbers are indicative, not a strict comparison.*

## Threats to validity

- Synthetic video: the test pattern's complexity is fixed, while real camera content varies.
- One 45 s run per cell. Small differences (for example 0 vs 1 freeze) are not significant. Repeated runs would give confidence intervals.
- netem `rate` uses a 1000-packet FIFO, which is larger than most real routers' queues (but similar to some mobile uplinks, as the NAT demo showed).
- Freeze counting uses WebRTC's definition, but our jitter buffer wait limits are our own choices, so freeze counts depend on those parameters.
