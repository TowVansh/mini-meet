# Mini-Meet

Video calls for 2–4 people, built on WebRTC, with a signalling server written for this project, NAT traversal through STUN, and call quality measured under emulated packet loss, jitter and bandwidth limits (tc netem).

Course project 16, Computer Networks (CO4, CO5, Modules 4 and 5).

| Doc | What it covers |
|---|---|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Media path vs signalling path, mesh topology, ICE/STUN, adaptation, security |
| [docs/PROTOCOL.md](docs/PROTOCOL.md) | Signalling protocol specification and sequence diagrams |
| [docs/EVALUATION.md](docs/EVALUATION.md) | Impairment method, results, discussion |
| [docs/NAT_DEMO.md](docs/NAT_DEMO.md) | How to run the cross-network STUN demo |
| [docs/SYLLABUS_MAP.md](docs/SYLLABUS_MAP.md) | Which syllabus topics each part of the project covers |
| [docs/VIVA_NOTES.md](docs/VIVA_NOTES.md) | Likely viva questions with answers |

## Quick start (Windows + WSL2 Ubuntu)

```bash
# inside WSL
cd /mnt/e/MiniMeet
npm install
npm start                 # http://localhost:8443
```

On Windows, open `http://localhost:8443` in two or more browser tabs (Chrome or Edge), use the same room name in each, and join. WSL2 forwards localhost, so Windows browsers can reach the server. Every remote tile shows live stats: candidate pair, RTT, bitrate, fps, loss, jitter, and the adaptation level.

For other devices on the LAN, the page must be HTTPS, because browsers only allow camera access on secure origins. Run `bash scripts/gen-cert.sh`, restart the server, and open `https://<pc-ip>:8443`. For calls across different networks, see [docs/NAT_DEMO.md](docs/NAT_DEMO.md).

## Tests

```bash
npm test     # signalling protocol integration tests (join, relay, room cap, validation, disconnect)
```

## Impairment evaluation

```bash
# from Windows (tc needs root inside WSL)
wsl -d Ubuntu -u root -- bash /mnt/e/MiniMeet/scripts/wsl-bench.sh
# options: PROFILES="baseline loss5" ADAPT="0 1" DURATION=45 PEERS=2 TAG=eval

# then
python3 analysis/plot.py results/eval_*.csv   # writes results/summary.md + graphs
```

Run one profile by hand: `sudo scripts/impair.sh loss5` (list them with `scripts/impair.sh list`, remove with `sudo scripts/impair.sh clear`).

## Layout

```
server/    signalling + static hosting + stats API (Node.js, express, ws)
client/    browser app (WebRTC mesh, stats sampler, AIMD adapter)
bench/     headless Chromium bots, impairment matrix runner
scripts/   netem profiles, cert generator, WSL bench wrapper
analysis/  summary + plots (pandas, matplotlib)
docs/      architecture, protocol, evaluation, viva notes
test/      server tests (node:test)
```
