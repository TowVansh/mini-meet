# Call-Quality Evaluation Under Network Impairment

## Method

- **Setup**: WSL2 Ubuntu 24.04 (kernel 6.18), Node 22. Two headless Chromium 131 bots in one call, using Chromium's synthetic camera and microphone, so the input is identical in every run.
- **Impairment**: `tc qdisc ... netem` on `lo` (`scripts/impair.sh`). Every media packet crosses `lo` once, so delay and jitter apply in each direction, and RTT ≈ 2 × the configured delay.
- **Procedure per run** (`bench/run-matrix.js`): clean call for 10 s, apply the profile, record 45 s, tear down. 14 profiles × adaptation {off, on} = 28 runs.
- **Metrics**: `getStats()` sampled once per second (`client/stats.js`). The table shows the steady state only (t ≥ 15 s): medians for bitrate, fps, height, jitter and RTT; means for loss and audio concealment. Freezes are Chromium's `freezeCount`, summed over both receivers. `bwe_kbps` is GCC's bandwidth estimate (`availableOutgoingBitrate`).
- **Reproduce**: `wsl -d Ubuntu -u root -- bash scripts/wsl-bench.sh`, then `python3 analysis/plot.py results/eval_*.csv`.

| Profile | netem arguments |
|---|---|
| baseline | none |
| loss1 / loss5 / loss10 / loss20 | `loss N%` (independent random loss) |
| burst5 | `loss gemodel 1% 30% 70% 0.1%` (bursty Gilbert-Elliott loss) |
| delay100 | `delay 100ms` |
| jitter30 / jitter60 | `delay 100ms 30ms` / `60ms`, normal distribution |
| rate2m / rate1m / rate500k / rate250k | `rate N` (bottleneck link with netem's default 1000-packet FIFO) |
| combo | `delay 80ms 20ms loss 3% rate 1mbit` |

## Results

| profile | adapt | video kbps | fps | height | video loss % | jitter ms | RTT ms | freezes | audio conceal % | BWE kbps | limit |
|---|---|---|---|---|---|---|---|---|---|---|---|
| baseline | off | 697 | 20 | 720 | 0.0 | 1 | 1 | 0 | 0.0 | 6146 | none |
| baseline | on | 695 | 20 | 720 | 0.0 | 1 | 1 | 0 | 0.0 | 6078 | none |
| loss1 | off | 702 | 20 | 720 | 1.1 | 1 | 1 | 0 | 0.6 | 5824 | none |
| loss1 | on | 695 | 20 | 720 | 1.1 | 1 | 1 | 0 | 0.5 | 6075 | none |
| loss5 | off | 699 | 20 | 720 | 5.1 | 1 | 1 | 0 | 4.5 | 5757 | none |
| loss5 | on | 476 | 20 | 480 | 4.7 | 1 | 1 | 0 | 4.4 | 2382 | none |
| loss10 | off | 656 | 20 | 720 | 9.2 | 1 | 1 | 1 | 7.8 | 1456 | none |
| loss10 | on | 120 | 20 | 180 | 8.5 | 0 | 1 | 0 | 8.2 | 1397 | none |
| loss20 | off | 65 | 20 | 225 | 16.1 | 5 | 1 | 5 | 13.8 | 143 | bandwidth |
| loss20 | on | 80 | 20 | 180 | 16.0 | 1 | 1 | 7 | 14.2 | 149 | none |
| burst5 | off | 689 | 20 | 720 | 2.0 | 2 | 1 | 0 | 1.4 | 6032 | none |
| burst5 | on | 318 | 20 | 360 | 1.9 | 1 | 1 | 0 | 1.7 | 2228 | none |
| delay100 | off | 669 | 20 | 720 | 0.0 | 2 | 202 | 1 | 0.0 | 4965 | none |
| delay100 | on | 664 | 20 | 720 | 0.0 | 0 | 201 | 2 | 0.0 | 5025 | none |
| jitter30 | off | 860 | 20 | 540 | 0.6 | 28 | 203 | 2 | 0.0 | 962 | bandwidth |
| jitter30 | on | 252 | 20 | 360 | 0.6 | 28 | 207 | 0 | 0.1 | 344 | bandwidth |
| jitter60 | off | 205 | 20 | 270 | 0.8 | 42 | 205 | 4 | 0.2 | 286 | bandwidth |
| jitter60 | on | 276 | 20 | 270 | 1.2 | 45 | 210 | 7 | 0.3 | 418 | bandwidth |
| rate2m | off | 238 | 20 | 540 | 0.0 | 8 | 5 | 0 | 0.0 | 367 | bandwidth |
| rate2m | on | 190 | 20 | 540 | 0.0 | 7 | 3 | 0 | 0.2 | 290 | bandwidth |
| rate1m | off | 54 | 20 | 180 | 0.0 | 8 | 5 | 5 | 0.0 | 125 | bandwidth |
| rate1m | on | 63 | 20 | 180 | 0.0 | 8 | 4 | 4 | 0.0 | 129 | bandwidth |
| rate500k | off | 36 | 20 | 180 | 0.0 | 20 | 12 | 12 | 1.1 | 88 | bandwidth |
| rate500k | on | 35 | 19 | 120 | 0.0 | 17 | 13 | 13 | 1.5 | 90 | bandwidth |
| rate250k | off | 30 | 13 | 180 | 4.5 | 58 | 11089 | 28 | 18.9 | 84 | bandwidth |
| rate250k | on | 30 | 16 | 68 | 8.4 | 65 | 10787 | 23 | 27.2 | 84 | bandwidth |
| combo | off | 38 | 20 | 180 | 3.0 | 21 | 210 | 17 | 1.7 | 96 | bandwidth |
| combo | on | 53 | 20 | 90 | 2.4 | 21 | 209 | 16 | 2.7 | 114 | bandwidth |

Graphs in `results/`: `bars_video_kbps.png`, `bars_freezes.png`, `bars_audio_conceal_pct.png`, `bars_rtt_ms.png`, and per-profile `timeline_<profile>.png` (send bitrate vs BWE, fps, loss and RTT over time, adaptation off vs on).

**4-party mesh check** (`results/mesh4_baseline_adapt1_p4.csv`): 4 bots, 6 links, all 12 directed video streams received at 720p, 290–480 kbps each, all `host->host`. Each bot sends 3 copies of its video, so per-stream bitrate is lower than in the 2-party baseline (~700 kbps). This is the uplink cost of mesh.

## How the system degrades

1. **Random loss up to 5 % costs almost nothing visible.** The video still arrives at 720p/20 fps with no freezes, because NACK retransmission recovers lost packets: at about 1 ms RTT there is plenty of time before playout. Audio concealment tracks the loss rate (0.6 % → 4.5 %), since Opus PLC/FEC fills the gaps rather than retransmitting.
2. **Loss is also a congestion signal to GCC.** At 10 % loss GCC's estimate drops from about 6 Mbit/s to 1.4 Mbit/s. At 20 % it collapses to about 140 kbps (`limit = bandwidth`), and video falls to about 200p with 5–7 freezes. GCC treats loss above 10 % as congestion even though netem's loss here is random, not caused by a queue.
3. **Pure delay (100 ms each way) barely matters.** RTT is 202 ms, but bitrate and resolution are unchanged. Steady latency only adds delay to conversation and NACK recovery; it does not reduce quality by itself.
4. **Jitter hurts more than delay.** With ±30 ms jitter, GCC's delay-gradient detector reads the variation as queue build-up: its estimate falls from about 5 Mbit/s to about 1 Mbit/s, and to about 300 kbps at ±60 ms. Freezes start appearing (2–7). Normally distributed netem jitter also reorders packets, which the jitter buffer has to absorb.
5. **Bandwidth caps are hit much harder than their nominal rate, because of bufferbloat.** netem's `rate` uses a 1000-packet FIFO. At 250 kbit/s that queue holds about 40 s of data, and RTT reached **about 11 s**. GCC backs off very conservatively when it sees queuing delay grow, so even at 2 Mbit/s the video settled near 240 kbps, far below the link rate. Below 1 Mbit/s, video drops to 120–180p with frequent freezes, and at 250 kbit/s audio concealment reaches 19–27 %. This is a textbook bufferbloat result: a large unmanaged FIFO turns a bandwidth limit into a latency disaster. An AQM queue (CoDel/fq_codel) or a small `limit` would keep RTT low.

## Effect of the AIMD adaptation controller

| Condition | Effect of adaptation (on vs off) |
|---|---|
| Clean, 1 % loss, pure delay | No change. The controller stays at level 0, as designed. |
| 5 % random and bursty loss | Drops to 480p/360p early. It loses resolution but gains no measurable benefit, because NACK was already coping. Too cautious here. |
| 10 % loss | Lands on the lowest rung (180p, 120 kbps). Freezes went from 1 to 0. |
| ±30 ms jitter | Freezes went from **2 to 0** at 360p. The best result for the controller: fewer packets per frame means fewer late frames. |
| ±60 ms jitter, 20 % loss | Slightly more freezes (7 vs 4–5). At this level GCC is already the binding limit, and stacking our cut on top adds keyframe churn from resolution changes. |
| Bandwidth caps, combo | Roughly the same. GCC dominates. Our controller lowers resolution but bitrate stays pinned by GCC's estimate. |

**Conclusion.** The controller helps where the problem is variation (jitter, or high loss near the threshold) and stays out of the way on clean links. But its 8 % loss threshold is too low for a network where NACK can still recover cheaply, so at 5 % loss it gives up resolution for nothing. A better rule would key on *unrecovered* loss (`freezeCount`, NACK/PLI counts, or the receiver's concealment) rather than raw RTCP loss. Under real bandwidth shortage, the browser's GCC is the controller that matters, and an application layer can only choose *how* to spend the bits it allows (resolution vs fps).

## NAT traversal results

Fill in from the live demo (see NAT_DEMO.md):

| Network A | Network B | Selected candidate pair | Connected? |
|---|---|---|---|
| | | | |

## Threats to validity

- The synthetic video (Chromium's moving test pattern) compresses much better than a real camera. A 720p stream needs only ~700 kbps, so absolute bitrates are lower than a real call's.
- netem on `lo` impairs both directions and also the signalling and stats HTTP traffic. Signalling was already finished before the impairment began.
- Each cell is a single 45 s run. GCC's behaviour has run-to-run variance, so small differences (for example 4 vs 5 freezes) are not significant. Repeating each run 3 times would allow confidence intervals.
- Both bots share one CPU, so encoder CPU limits could interact. `qualityLimitationReason` never reported `cpu`, which suggests this did not happen.
