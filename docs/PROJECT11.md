# Project 11 Checklist: Real-Time Adaptive Video/Voice Streaming

Mini-Meet also covers Project 11 (CO4; Modules 1, 2, 4). The table maps each item in that brief to the code, and to the results that show it working.

## Expected deliverables

| Deliverable | Where it is | Evidence |
|---|---|---|
| Sender/receiver pair streaming **live or recorded** media over UDP | `mm` sends and receives at the same time. Live: webcam + mic (`media_in.c` `open_camera`/`open_mic`). Recorded: `--file media/sample.mp4`, paced to real time by its timestamps (`pace_file_frame`). | Live calls on Windows; file streaming at 20 fps with 0 % audio concealment (log line `recorded file: …`) |
| **Custom framing-layer specification** with sequence numbers and timestamps | PROTOCOL.md §2: RTP-style header, our payload types (96 VP8, 97 retransmission, 111 Opus), 1-byte frame descriptor (S/K bits), field rules, MTU splitting, encryption layout. Code: `rtp.c`, `media_in.c` `send_video_frame`. | `make test` (header, wrap-around, RTCP round trips) |
| **Jitter buffer** with measured playout smoothness under induced jitter | Playout clock (95th-percentile offset, `rtp.c` `playout_*`), video frames held until due, audio frames due every 20 ms (`media_out.c` `video_tick`, `audio_tick`). Measured per second: `playout_sd_ms`, `jb_delay_ms`, freezes, audio concealment. | EVALUATION.md "Jitter buffer": ±30/60 ms jitter, buffer on vs off (`--no-jb`) |
| Sequence-number-based **loss concealment** | Gaps → NACK + retransmission. Unrecoverable video → skip + keyframe request (PLI). Audio → Opus FEC from the next packet, else PLC. | Loss 1–20 % rows of EVALUATION.md |
| **Bitrate adaptation loop** across changing network conditions | Receiver feedback = RTCP receiver reports (loss, jitter, RTT) every second → AIMD controller with slow start and feedback-starvation detection (`cc.c`, `netloop.c` `on_rtcp`/`check_feedback`) → encoder bitrate and resolution (`media_in.c` `venc_open`). | EVALUATION.md "Changing network conditions" (`results/timeline_schedule.png`: 2 → 1 → 0.5 → 1 Mbit/s → clean in one call) |
| **Quality-vs-impairment report** with objective metrics | EVALUATION.md: 14 netem profiles × adaptation on/off. Metrics: bitrate, fps, resolution, loss, jitter, RTT, freezes, audio concealment, playout spread, buffer delay. | `results/summary.md`, `results/*.png` |

## Milestones

| Milestone | Done |
|---|---|
| Media capture/encode pipeline; UDP framing layer | FFmpeg capture → VP8/Opus → RTP over UDP (`media_in.c`, `rtp.c`, `net.c`) |
| Jitter buffer and loss concealment at the receiver | `media_out.c`, `rtp.c` `playout_*` |
| Receiver feedback channel and bitrate adaptation | RTCP SR/RR/NACK/PLI (`rtp.c`, `netloop.c`), `cc.c` |
| Impairment evaluation with tc netem, report | `bench/run-matrix.sh`, `scripts/impair.sh`, `analysis/plot.py`, EVALUATION.md |

## Required tools

C (the brief allows Python/C++), FFmpeg (libavformat/libavcodec capture and decode), UDP sockets (`src/common/net.c`, Winsock and POSIX), tc netem (`scripts/impair.sh`), and Git (github.com/TowVansh/mini-meet).
