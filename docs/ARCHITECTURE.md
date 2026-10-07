# Mini-Meet Architecture

## Overview

Mini-Meet is a 2–4 person audio/video calling app. It has two separate paths:

| | Signalling path | Media path |
|---|---|---|
| Purpose | Find peers, exchange SDP and ICE candidates | Carry audio and video |
| Endpoints | Browser ↔ Node.js server | Browser ↔ browser, directly |
| Transport | WebSocket over TCP (TLS when `wss://`) | RTP/RTCP over SRTP, over DTLS-negotiated keys, over UDP |
| Volume | A few KB per call setup | 0.1–2 Mbit/s per stream, continuous |
| Reliability need | Every message must arrive, in order, so TCP fits | Late data is useless, so UDP fits; loss is concealed or repaired |
| If server dies | New joins fail | Calls already running keep working |

```
                    +-----------------------------+
                    |  Node.js signalling server  |
                    |  express (static client)    |
                    |  ws  /ws  (rooms, relay)    |
                    |  /api/stats  -> results/*.csv
                    +-----------------------------+
                       ^  WebSocket (TCP)   ^
         signalling    |                    |    signalling
                       v                    v
 +------------------------+          +------------------------+
 | Browser A              |  SRTP /  | Browser B              |
 | getUserMedia           |  UDP     | getUserMedia           |
 | RTCPeerConnection(B) <==========> RTCPeerConnection(A)     |
 | StatsSampler, Adapter  |  media   | StatsSampler, Adapter  |
 +------------------------+          +------------------------+
            ^                                    ^
            |  STUN binding request (UDP 19302)  |
            +--------> STUN server <-------------+
                      (stun.l.google.com)
```

## Components

| File | Role |
|---|---|
| `server/index.js` | HTTP(S) server, static client, WebSocket endpoint, heartbeat, stats API |
| `server/protocol.js` | Message schema and validation, error codes |
| `server/rooms.js` | Room registry: join/leave, room cap, relaying within a room only |
| `server/stats.js` | Writes stats rows to CSV |
| `client/signalling.js` | WebSocket client for the protocol |
| `client/peer.js` | Mesh of `RTCPeerConnection`s, offer/answer, queuing ICE candidates that arrive early |
| `client/stats.js` | `getStats()` sampler, turns counters into per-second rates, overlay text |
| `client/adapt.js` | AIMD quality controller |
| `client/app.js` | UI, local media, join/leave, stats upload |
| `bench/*.js` | Headless bots and the impairment matrix |
| `scripts/impair.sh` | `tc netem` profiles |
| `analysis/plot.py` | Summary tables and graphs |

## Media path in detail

1. **Capture**: `getUserMedia` gives one audio track (48 kHz) and one video track (up to 1280×720 at 30 fps).
2. **Encode**: Opus for audio (in-band FEC on), VP8 for video. These are negotiated in SDP.
3. **Packetise**: RTP. Sequence numbers let the receiver detect loss. Timestamps drive the jitter buffer.
4. **Secure**: DTLS handshake on the media 5-tuple, which derives SRTP keys. Every media packet is encrypted and authenticated. The SDP carries the DTLS certificate fingerprint, so the signalling channel ties the keys to the participants.
5. **Transport**: UDP on the ICE-selected candidate pair. RTP and RTCP are multiplexed on one port (rtcp-mux), and all tracks share one 5-tuple (BUNDLE).
6. **Feedback**: RTCP receiver reports (loss fraction, jitter, used to compute RTT), NACK (retransmit request), PLI (ask for a keyframe), and transport-wide congestion control feedback (per-packet arrival times).
7. **Receive**: The jitter buffer reorders packets and absorbs delay variation. Opus PLC/FEC conceals lost audio. Video freezes until a keyframe arrives if a frame cannot be decoded.

## Topology: full mesh

Each participant opens one `RTCPeerConnection` per other participant.

| Participants | Links in call | Uplink streams per person |
|---|---|---|
| 2 | 1 | 1 |
| 3 | 3 | 2 |
| 4 | 6 | 3 |

Mesh needs no media server, gives the lowest latency (one hop), and keeps end-to-end encryption. Its cost is uplink: each person encodes and sends N−1 copies. At 4 people with about 1 Mbit/s video that is about 3 Mbit/s up, which is why the cap is 4. Alternatives:

- **SFU** (Selective Forwarding Unit): each client sends one stream to a server, which forwards it to the others. Uplink stays constant. Needs a server on the media path. Used by Google Meet and Zoom.
- **MCU**: the server decodes, mixes and re-encodes. Lowest client load, highest server cost and latency.

## NAT traversal (ICE + STUN)

Each browser collects candidate addresses:

- **host**: its own interface IP (for example `192.168.1.5:54321`).
- **srflx** (server-reflexive): the public IP:port its NAT assigned, learned by sending a STUN Binding Request to the STUN server and reading `XOR-MAPPED-ADDRESS` from the reply.
- **relay**: an address on a TURN server. Not used here. TURN is the fallback when both sides are behind symmetric NAT.

Candidates are exchanged over signalling. ICE then runs STUN connectivity checks on every candidate pair, sending packets in both directions at the same time. This "hole punching" opens mappings on both NATs. The best working pair is nominated. The stats overlay shows the chosen pair as `local-type->remote-type` (for example `srflx->srflx` across two networks, `host->host` on one LAN).

**Limit**: a symmetric NAT gives a different public port for each destination, so the port learned from STUN does not match the one the peer sees. Two symmetric NATs, or one symmetric and one port-restricted, cannot connect without TURN. Mobile carrier NAT (CGNAT) is often symmetric. This is recorded as an expected failure case in EVALUATION.md.

## Quality adaptation (two layers)

1. **Built in (browser)**: Google Congestion Control (GCC). It estimates available bandwidth from transport-wide feedback (delay-gradient based) and from loss, and sets the encoder target bitrate. Its estimate appears as `availableOutgoingBitrate`. When bitrate drops, the encoder lowers resolution or fps on its own (`qualityLimitationReason = bandwidth`).
2. **Ours**: `client/adapt.js`, an AIMD controller per outgoing video stream. It reads receiver-reported loss and RTT once per second:
   - loss > 8 % or RTT > 400 ms: drop **two** ladder levels (multiplicative decrease)
   - 3 clean seconds in a row (loss < 2 %, RTT < 250 ms): climb **one** level (additive increase)
   - Ladder: 1500k@1x, 900k@1x, 600k@1.5x, 350k@2x, 200k@3x, 120k@4x (max bitrate @ downscale factor)
   - Applied with `RTCRtpSender.setParameters()` (`maxBitrate`, `scaleResolutionDownBy`).

   Why add it on top of GCC: GCC mostly reacts to delay. Under *random* loss (Wi-Fi, not congestion) GCC keeps sending at a high rate, and every lost packet of a large frame breaks that frame. Our controller trades resolution for fewer packets per frame and fewer freezes. Under real congestion it backs off sooner. The evaluation compares adaptation on and off.

## Security

- Media: DTLS-SRTP is mandatory in WebRTC, so it is always encrypted end to end between browsers. In mesh, the server cannot decrypt media.
- Signalling: should run over `wss://` (TLS) so that SDP, including the DTLS fingerprints, cannot be swapped by a man-in-the-middle. Use `scripts/gen-cert.sh` or a TLS tunnel.
- The server validates every message (schema, size, room name) and relays only within a room, so a client cannot inject an offer into another room.
- Not in scope: authentication or room passwords. Anyone who knows the room name can join.

## Measurement pipeline

```
getStats() every 1 s -> StatsSampler (per-interval deltas) -> rows
  -> on-screen overlay
  -> POST /api/stats every 5 s -> results/<run>.csv
  -> analysis/plot.py -> summary.md + PNG graphs
```

Impairment runs (bench/run-matrix.js, inside WSL2):

```
 headless Chromium (bot A) <-- lo (tc netem: loss/delay/jitter/rate) --> headless Chromium (bot B)
```

Both bots run on one Linux host, so the media crosses the `lo` interface, where netem shapes it. They use Chromium's synthetic camera and mic, so the input is the same in every run.
