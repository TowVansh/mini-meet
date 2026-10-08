# Mini-Meet

Video calls for 2–4 people, **written in C**, for Windows and Linux. It has its own signalling server and protocol, its own STUN client and server for NAT traversal, and RTP/RTCP media over raw UDP with jitter buffers, NACK retransmission, Opus FEC and an AIMD rate controller. Call quality was measured under emulated packet loss, jitter and bandwidth limits (tc netem).

Course project 16, Computer Networks (CO4, CO5; Modules 4 and 5).

| Doc | What it covers |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Media path vs signalling path, threads, demultiplexing, ICE/STUN, loss repair, rate control |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Signalling protocol spec and the RTP/RTCP/STUN media profile |
| [docs/EVALUATION.md](docs/EVALUATION.md) | Impairment method, results, comparison with the WebRTC prototype |
| [docs/NAT_DEMO.md](docs/NAT_DEMO.md) | How to run a call across two different networks |
| [docs/SYLLABUS_MAP.md](docs/SYLLABUS_MAP.md) | Which syllabus topics each part covers |
| [docs/VIVA_NOTES.md](docs/VIVA_NOTES.md) | Likely viva questions with answers |

## Build

**Windows** (MSYS2, "MSYS2 MINGW64" shell):
```bash
pacman -S --needed make mingw-w64-x86_64-{gcc,pkgconf,ffmpeg,SDL2,libvpx,opus}
make            # bin/mm-server.exe, bin/mm-stun.exe, bin/mm.exe
make test       # unit tests
make dist       # copy DLLs into bin/ so it runs on PCs without MSYS2
```

**Linux / WSL** (Ubuntu):
```bash
sudo apt install build-essential pkg-config libavdevice-dev libavformat-dev libavcodec-dev \
     libswscale-dev libswresample-dev libavutil-dev libsdl2-dev libvpx-dev libopus-dev
make && make test
```

## Run a call

```bash
bin/mm-server                                   # signalling server, TCP 9000
bin/mm --server 127.0.0.1 --room demo --name Alice             # webcam + mic
bin/mm --server 127.0.0.1 --room demo --name Bob --test        # test pattern + tone
```

Up to 4 clients per room. On another PC, use `--server <server-ip>`. Keys in the window: **M** mute, **V** camera off, **S** stats overlay, **Q** quit. Run `bin/mm --list-devices` to see cameras and microphones, then choose with `--video-dev "..." --audio-dev "..."`.

Each remote tile shows the selected candidate pair (`host->host`, `srflx->srflx`), RTT, resolution, loss, jitter, freezes and the loss the other side reports. Add `--csv stats.csv` to log every second.

Windows Firewall asks the first time `mm.exe` / `mm-server.exe` run. Allow both, and tick **public** networks too if you are on college Wi-Fi.

## Evaluation

```bash
# from Windows; tc netem needs root inside WSL
wsl -d Ubuntu -u root -- bash /mnt/e/MiniMeet/bench/run-matrix.sh
python3 analysis/plot.py results/eval_*.csv     # summary.md + graphs in results/
```

`sudo scripts/impair.sh <profile>` applies a single impairment by hand (`scripts/impair.sh list` shows the profiles).

## Layout

```
src/common/   portable sockets (Winsock/POSIX), clock, signalling line protocol
src/server/   mm-server
src/stun/     STUN encode/decode + mm-stun server
src/client/   mm: ICE, RTP/RTCP, jitter buffers, codecs, rate control, SDL UI
tests/        unit tests (protocol parsing, STUN, RTP/RTCP, receiver stats, controller)
bench/        netem impairment matrix
analysis/     summary + plots
results/      measurement data (results/v1 = WebRTC prototype)
legacy-webrtc/ first prototype (browser WebRTC + Node.js), kept for comparison
```
