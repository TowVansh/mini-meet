# Call-Quality Evaluation Under Network Impairment (C client)

## Method

- **Testbed** (`bench/run-matrix.sh`): WSL2 Ubuntu 24.04. Two Linux **network namespaces** (`mmA` 10.10.0.1, `mmB` 10.10.0.2) are joined by a veth pair, so they behave like two separate hosts. `mm-server` and client botA run in `mmA`, botB in `mmB`.
- **Impairment**: `tc netem` on **both** veth ends (`scripts/impair.sh`), so each direction is impaired separately, like a real access link. A "1 Mbit/s" profile is 1 Mbit/s each way, and `delay 100ms` gives RTT ≈ 200 ms.
- **Media**: the built-in test pattern (VP8, up to 640×480 at 20 fps) and a test tone (Opus 32 kbps). Same input every run. End-to-end encryption and the jitter buffer are on.
- **Procedure**: start the call → 10 s clean → apply the profile **mid-call** → record 45 s → clear. 14 profiles × {adaptation off (`--no-adapt`, fixed 1200 kbps), on} = 28 runs. Two extra experiments: jitter buffer on vs off (`--no-jb`), and one call through **changing** conditions (`SCHEDULE=`).
- **Metrics** (per second, per client CSV): bitrate, fps, resolution, loss, jitter, RTT, freezes, audio concealment, **playout spread** (`playout_sd_ms`: standard deviation of the intervals between shown frames; 0 = perfectly even) and **jitter-buffer delay** (`jb_delay_ms`). Tables show steady state (t ≥ 15 s): medians, except loss and concealment (means) and freezes (sum over both receivers).
- **Reproduce**: see README "Evaluation".

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

| profile | adapt | video kbps | fps | height | loss % | jitter ms | RTT ms | freezes | audio conceal % | playout sd ms | audio buffer ms |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | off | 1205 | 20 | 480 | 0.0 | 0.1 | 0.2 | 0 | 0.0 | 1.4 | 27 |
| baseline | on | 1202 | 20 | 480 | 0.0 | 0.1 | 0.7 | 0 | 0.0 | 1.5 | 27 |
| loss1 | off | 1212 | 20 | 480 | 1.0 | 0.1 | 0.3 | 0 | 0.0 | 2.2 | 27 |
| loss1 | on | 1211 | 20 | 480 | 0.9 | 0.1 | 0.2 | 0 | 0.0 | 1.9 | 27 |
| loss5 | off | 1224 | 20 | 480 | 4.9 | 0.1 | 0.3 | 0 | 0.2 | 16.2 | 27 |
| loss5 | on | 685 | 20 | 360 | 5.1 | 0.2 | 0.3 | 0 | 0.4 | 16.3 | 26 |
| loss10 | off | 1218 | 20 | 480 | 9.9 | 0.1 | 0.9 | 2 | 1.3 | 23.2 | 27 |
| loss10 | on | 106 | 20 | 120 | 9.2 | 0.3 | 0.3 | 0 | 1.1 | 23.4 | 26 |
| loss20 | off | 1222 | 19 | 480 | 19.8 | 0.1 | 0.2 | **23** | 4.0 | 38.0 | 27 |
| loss20 | on | 104 | 20 | 120 | 20.8 | 0.2 | 0.3 | **15** | 3.2 | 40.8 | 26 |
| burst5 | off | 1188 | 20 | 480 | 2.5 | 0.1 | 4.6 | 0 | 0.7 | 3.8 | 27 |
| burst5 | on | 1224 | 20 | 480 | 2.1 | 0.1 | 4.0 | 0 | 0.5 | 2.3 | 26 |
| delay100 | off | 1218 | 20 | 480 | 0.0 | 0.1 | 200 | 0 | 0.0 | 1.4 | 27 |
| delay100 | on | 580 | 20 | 360 | 0.0 | 0.2 | 200 | 0 | 0.0 | 1.0 | 26 |
| jitter30 | off | 1372 | 20 | 480 | 1.0 | 33.5 | 187 | 1 | 0.1 | 12.4 | 140 |
| jitter30 | on | 278 | 20 | 240 | 0.8 | 33.8 | 200 | 0 | 0.1 | 4.7 | 143 |
| jitter60 | off | 1634 | 20 | 480 | 1.7 | 65.7 | 194 | 4 | 0.2 | 22.7 | 222 |
| jitter60 | on | 213 | 20 | 240 | 1.5 | 70.5 | 211 | 3 | 0.1 | 8.1 | 220 |
| rate2m | off | 1228 | 20 | 480 | 0.0 | 7.8 | 47 | 0 | 0.0 | 1.3 | 56 |
| rate2m | on | 1226 | 20 | 480 | 0.0 | 7.6 | 39 | 0 | 0.0 | 1.5 | 56 |
| **rate1m** | off | 923 | 15 | 480 | 13.6 | 13.6 | **10513** | 0 | **34.1** | 2.1 | 387 |
| **rate1m** | on | 198 | **20** | 240 | **0.0** | 3.4 | **2.8** | 0 | **2.7** | 1.2 | 28 |
| **rate500k** | off | 446 | **0** | 480 | 39.6 | 22.9 | **19414** | 2 | **49.0** | 5.8 | 476 |
| **rate500k** | on | 188 | **20** | 120 | **0.0** | 3.2 | **9.8** | 0 | 11.7 | 2.5 | 38 |
| rate250k | off | 229 | 4 | 480 | 24.7 | 31.6 | 17373 | 2 | 74.5 | 7.0 | 5073 |
| rate250k | on | 164 | 9 | 120 | 1.0 | 18.8 | 11707 | 6 | 34.8 | 5.2 | 476 |
| **combo** | off | 924 | **0** | 480 | 13.8 | 13.9 | **10054** | 9 | 33.0 | 2437 | 349 |
| **combo** | on | 231 | **20** | 240 | 2.7 | 14.0 | **199** | 64 | 0.3 | 41.6 | 82 |

Graphs in `results/`: `bars_*.png` (each metric, adaptation off vs on) and `timeline_<profile>.png` (send rate vs target, fps, loss, RTT, and jitter buffer over time).

**4-peer mesh** (`results/mesh4_baseline_adapt1_p4__*.csv`, Windows): all **12 directed video streams** received at 640×480, 20 fps, about 1140–1200 kbps each. Each client uploads 3 copies (~3.6 Mbit/s), which is the cost of mesh.

**Real cross-network call** (NAT_DEMO.md): college Wi-Fi ↔ laptop on a mobile hotspot, `srflx->srflx`, median RTT 85 ms with spikes to 1.15 s from the hotspot's uplink buffer. The controller followed the spikes down and back up.

**Recorded media** (`--file media/sample.mp4`, H.264 + AAC, 20 s, looped): streamed in real time on Linux and Windows. The viewer received 640×480 at 20 fps with 0 % audio concealment and 1.9 ms playout spread.

## Jitter buffer: playout smoothness under induced jitter

The same jitter profiles were run with the jitter buffer **off** (`--no-jb`: playout follows the fastest packet with only a 25 ms margin) and **on** (95th-percentile playout clock). Data: `results/nojb_*` vs `results/eval_jitter*`.

| Jitter (each direction) | Buffer | Audio concealed | Video playout spread | Freezes | Buffer delay (audio) |
|---|---|---|---|---|---|
| ±30 ms | off | **87.9 %** | 25.2 ms | 1 | 25 ms |
| ±30 ms | **on** | **0.1 %** | **12.4 ms** | 1 | 140 ms |
| ±60 ms | off | **83.6 %** | 40.7 ms | 13 | 25 ms |
| ±60 ms | **on** | **0.2 %** | **22.7 ms** | 4 | 222 ms |
| ±60 ms, adaptation on | off | 84.2 % | 53.0 ms | 32 | 25 ms |
| ±60 ms, adaptation on | **on** | 0.1 % | 8.1 ms | 3 | 220 ms |

- **Without a buffer, jitter makes audio unusable**: most packets arrive after their 20 ms slot and are concealed. Video frames are shown as unevenly as they arrive.
- **With the buffer**, concealment falls to about 0 %, playout spread halves or better, and freezes fall (32 → 3 in the worst case).
- **The cost is latency.** The buffer grows by itself to ~140 ms (±30 ms) and ~220 ms (±60 ms) of added delay, and stays at ~27 ms on a clean link (25 ms margin + ~2 ms). This is the basic jitter-buffer trade-off: smoothness vs delay, decided here by the 95th percentile.
- The remaining playout spread at ±60 ms comes from the ~5 % of frames that arrive later than the 95th percentile, plus retransmitted frames.

## Bitrate adaptation across changing network conditions

One call, with the network changing every ~20 s: clean → 2 Mbit/s → 1 Mbit/s → 500 kbit/s → 1 Mbit/s → clean (`results/timeline_schedule.png`, phase lines marked).

- **Adaptive sender**: at 2 Mbit/s it stays at the top level. When the link drops to 1 Mbit/s it cuts to the bottom level within ~5 s, then probes back up to ~500 kbps. At 500 kbit/s it backs off again. At 1 Mbit/s it probes up to the top level, overshoots and backs off: the AIMD sawtooth. Once the network is clean it climbs back to 1200 kbps within ~17 s. RTT peaks at about 1 s right after each step down and then returns to tens of ms. Fps stays at 20 apart from short dips at those steps.
- **Fixed-rate sender**: RTT climbs to **14 s** as the queue fills, video drops to 0–15 fps, and audio breaks up. It only recovers once the network is clean again.

The same experiment found a bug, now fixed. Under 15 s of queueing a client's PINGs to the signalling server were stuck in the queue, and the server timed the client out. The peer then hung up even though its media was still flowing. Clients now keep a peer while its media still arrives (PROTOCOL.md "Liveness"). The fix triggered in two fixed-rate runs (rate250k, schedule) and kept those calls alive.

## How the system degrades

1. **Random loss up to 5 % is invisible.** The video stays at 640×480/20 fps with no freezes: NACK retransmission recovers lost packets before their deadline. Audio concealment stays ≤ 0.4 % thanks to Opus FEC.
2. **At 10–20 % loss, video breaks down.** Some frames lose a packet *and* its retransmission. The frame is skipped, later frames reference it, and the receiver waits for a keyframe (PLI): a freeze. That is 23 freezes at 20 % loss without adaptation. Audio holds up (4 % concealed).
3. **Constant delay costs nothing.** At 200 ms RTT, quality equals baseline.
4. **Jitter is absorbed by the jitter buffer** (section above), at the cost of 140–220 ms of extra delay. The received bitrate exceeds the send rate (1634 vs 1200 kbps at ±60 ms), because heavy reordering still triggers some unnecessary retransmissions.
5. **Bandwidth caps without rate control are catastrophic: bufferbloat.** 1200 kbps into a 1 Mbit/s link fills netem's 1000-packet FIFO. RTT climbs to **10.5 s**, then the full queue drops packets (14–40 %), video stops (0 fps at 500 k) and audio breaks up (34–75 % concealed).

## Effect of the rate controller

| Situation | Result |
|---|---|
| **1 Mbit/s, 500 kbit/s caps, combo, changing conditions** | **Decisive win.** RTT stays at **3–10 ms instead of 10–19 s**, loss is 0 %, video runs at 20 fps instead of freezing, and audio is mostly clean. In combo the fixed-rate call stalls completely (0 fps, RTT 10 s), while the adaptive one keeps running at 20 fps and 199 ms. |
| Clean, 1 % loss, bursty loss, 2 Mbit/s | No change: it stays at the top level, as designed. |
| Fixed +100 ms delay | Short dip, then recovery: the 10 s windowed minimum learns the new path delay. An early version without the window stayed at the bottom forever; this test caught that bug. |
| 10–20 % random loss | Fewer freezes (2 → 0, 23 → 15): smaller frames have fewer packets that can be lost. |
| 5 % random loss | **Over-reacts.** It cannot tell random (wireless) loss from congestion, so it drops to ~690 kbps although NACK was coping. This is the classic weakness of loss-based control on wireless links. |
| Jitter ±30/±60 ms | **Over-reacts** (random delay variation looks like queuing delay) and drops to 180–300 kbps. Thanks to the jitter buffer, this no longer costs smoothness. |
| 250 kbit/s cap (sudden drop mid-call) | **Reacts fast but cannot empty the queue in time.** Feedback-starvation detection backs off within 2.5 s and reaches the bottom rung within ~4 s. The first 2.5 s at 1200 kbps already left megabits in netem's 1000-packet queue, though, and the lowest rung plus audio (~180 kbps on the wire) drains only ~70 kbps of it. RTT stays high, but audio improves (75 % → 35 % concealed). The fix would be an emergency "pause video" rung, or AQM (CoDel) in the network. |

**Conclusion.** Real-time media over UDP needs both halves of the loop. **Rate control** keeps the sending rate under the bottleneck (without it, any slower link turns into seconds of delay and then massive loss). The **jitter buffer** turns uneven arrivals back into even playout (without it, ±30 ms of jitter conceals ~88 % of audio). The remaining weaknesses are textbook ones: random loss and jitter are misread as congestion, and a huge network buffer cannot be emptied by the sender alone.

## Comparison with the WebRTC prototype (`results/v1/`, `legacy-webrtc/EVALUATION-webrtc.md`)

| | WebRTC (Chrome GCC) | Mini-Meet C (AIMD) |
|---|---|---|
| Random loss ≤ 5 % | unaffected | unaffected (adaptation off); over-reacts (on) |
| 1 Mbit/s cap | GCC settled very low (~50 kbps), RTT small | ~200 kbps at 20 fps, RTT 3 ms |
| 250 kbit/s cap | RTT ~11 s | RTT ~12 s (both stuck behind the same netem buffer) |
| Visibility | browser black box | every mechanism in our own C code |

*v1 ran on a single shared `lo` queue and the C runs on per-direction veth links, so the comparison is indicative only.*

## Threats to validity

- Synthetic video: the test pattern's complexity is fixed, while real camera content varies.
- One 45 s run per cell. Small differences (0 vs 1 freeze) are not significant, and the rate250k case varies between runs.
- netem `rate` uses a 1000-packet FIFO, larger than most router queues (but similar to some mobile uplinks, as the NAT demo showed).
- Freeze counts and playout spread depend on our jitter-buffer settings (95th percentile, 25 ms audio margin).
- The changing-conditions run is a single call per setting, so read it as a demonstration, not a statistic.
