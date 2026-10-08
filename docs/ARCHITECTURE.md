# Mini-Meet Architecture (C version)

Mini-Meet is a 2–4 person audio/video calling application written in C. It runs on Windows and Linux. The networking code is written from scratch on plain sockets: signalling, STUN, NAT hole punching, RTP/RTCP, jitter buffers, retransmission and rate control. Libraries are used only for capture and codecs: FFmpeg (camera/mic input), libvpx (VP8 video), libopus (audio) and SDL2 (window and speakers).

## Programs

| Binary | Source | Role |
|---|---|---|
| `mm-server` | `src/server/mm_server.c` | TCP signalling server. Rooms of up to 4. Relays candidates. Never touches media. |
| `mm-stun` | `src/stun/mm_stun.c` | Optional STUN server (RFC 5389 Binding), so the project does not have to rely on Google's |
| `mm` | `src/client/*.c` | The call client: capture, encode, send, receive, decode, display |

## Two separate paths

| | Signalling path | Media path |
|---|---|---|
| Purpose | Find peers, exchange network candidates | Carry audio and video |
| Endpoints | client ↔ `mm-server` | client ↔ client directly (full mesh) |
| Transport | TCP, newline-delimited text protocol | UDP: RTP + RTCP + STUN on **one** port |
| Volume | A few hundred bytes per call setup | 0.1–1.3 Mbit/s per stream, continuous |
| Reliability need | Every line must arrive, in order, so TCP fits | Late data is useless, so UDP fits; loss is repaired selectively (NACK, FEC) or concealed |
| If the server dies | New joins fail | Calls already running keep going |

```
                 +--------------------------+
                 |  mm-server  (TCP 9000)   |
                 |  select() loop, rooms    |
                 +--------------------------+
                    ^   text lines (TCP)   ^
                    |                      |
 +------------------+---+              +---+------------------+
 | mm (A)               |   RTP/RTCP   | mm (B)               |
 | capture -> VP8/Opus  | <==========> | capture -> VP8/Opus  |
 | jitter buf -> decode |  UDP, P2P    | jitter buf -> decode |
 | SDL window/audio     |              | SDL window/audio     |
 +----------------------+              +----------------------+
            |   STUN Binding (UDP)              |
            +------> STUN server <--------------+
                 (stun.l.google.com or mm-stun)
```

## Client internals (`src/client`)

| File | What it does |
|---|---|
| `main.c` | Command-line options, startup and shutdown |
| `sig.c` | Signalling client: TCP connect, line assembly, JOIN/CAND/PING |
| `ice.c` | Candidate gathering (host + STUN srflx), connectivity checks, hole punching, keepalive |
| `netloop.c` | Network thread: `select()` over TCP + UDP, packet demultiplexing, RTCP send/receive, stats/CSV |
| `rtp.c` | RTP header, RTCP SR/RR/NACK/PLI encoding and decoding, RFC 3550 receiver statistics |
| `media_in.c` | Capture (camera/mic/file/test pattern), VP8/Opus encoding, packetization, retransmission history |
| `media_out.c` | Video and audio jitter buffers, NACK/PLI generation, decoding, freeze detection, SDL window, audio mixing |
| `cc.c` | AIMD rate controller and its bitrate/resolution ladder |
| `font.c` | 3×5 pixel font for on-screen text (no font library) |

Threads: **main** (SDL window/events), **net** (all socket I/O and timers), **video-in** and **audio-in** (capture plus encode). One mutex (`g.lock`) protects the peer table and the sender state.

## Packet demultiplexing on one UDP port (RFC 7983)

Every datagram arrives on the same socket. The first bytes decide what it is:

| First byte | Second byte | Meaning |
|---|---|---|
| 0–3 and magic cookie `0x2112A442` at offset 4 | — | STUN (NAT discovery, connectivity checks, keepalive) |
| 128–191 | 200–206 | RTCP (SR, RR, NACK, PLI) |
| 128–191 | other | RTP: PT 96 = VP8, PT 97 = VP8 retransmission, PT 111 = Opus |

The source address then identifies which peer the packet came from.

## NAT traversal (simplified ICE)

1. **Gather**: the host candidate is the LAN IP plus our UDP port. The **srflx** candidate comes from sending a STUN Binding request to the STUN server and reading `XOR-MAPPED-ADDRESS`, our public IP:port as seen from outside our NAT.
2. **Exchange**: `CAND <peer> <type> <ip> <port>` lines through the signalling server.
3. **Check**: both peers send STUN Binding requests to every candidate of the other every 100 ms, with `USERNAME "<their-id>:<my-id>"`. Our outgoing packet opens a mapping in our own NAT, so the other side's packets can come back in. This is **UDP hole punching**. A request from an unknown address is added as a **prflx** (peer-reflexive) candidate.
4. **Select**: the first candidate that answers becomes the media address. The UI and CSV show the pair, for example `srflx->srflx`.
5. **Keepalive**: a Binding request every 2.5 s keeps NAT mappings from expiring.
6. **Failure**: if nothing answers within 15 s, the client reports that both sides are probably behind symmetric NAT and a TURN relay would be needed (out of scope).

## Media path

```
camera --FFmpeg--> I420 --libswscale--> ladder resolution --libvpx VP8 (CBR, realtime)--> frame
frame --split into <=1160-byte payloads--> [RTP hdr | 1-byte descriptor (S,K) | VP8 data] --UDP--> each peer

mic --FFmpeg--> PCM --libswresample--> 48 kHz mono --libopus (32 kbps, 20 ms, in-band FEC)--> [RTP hdr | Opus] --UDP-->
```

- **RTP header** (RFC 3550, 12 bytes): version 2, marker (last packet of a video frame), payload type, 16-bit sequence number, timestamp (90 kHz video, 48 kHz audio), SSRC.
- **Video descriptor byte**: `S` marks the first packet of a frame, `K` marks a keyframe.
- **MTU**: payloads are capped so IP packets stay around 1200 bytes, well under a 1500-byte Ethernet MTU. This avoids IP fragmentation, where losing one fragment loses the whole datagram.
- **Mesh**: one encoder, and each RTP packet is sent to every connected peer (N−1 copies). The encoder runs at the level of the *worst* receiver. Simulcast or SVC would avoid that trade-off, and an SFU would avoid the uplink copies.

## Receive side and loss repair

| Mechanism | How it works |
|---|---|
| **Sequence gap → NACK** | A jump in sequence numbers immediately puts the missing numbers on a NACK list. A generic NACK (RTCP 205/1, packet ID + 16-bit bitmask) is resent every 1.5×RTT, up to 4 tries. |
| **Retransmission** | The sender keeps the last 1024 video packets. On NACK it resends them with **PT 97**, so the receiver can keep them out of loss and jitter statistics. Statistics then show network loss before repair, not after. |
| **Video jitter buffer** | Packets go into a ring indexed by `seq % 1024`. A frame is decoded only when every packet from `S` to marker is present. If a hole is not filled within max(120 ms, 2.5×RTT + 30 ms), the frame is skipped. |
| **PLI** | After a skip or a decode error, later frames reference missing data, so the receiver drops frames until a keyframe arrives and sends a PLI (RTCP 206/1) to request one (rate-limited to one per 300 ms). |
| **Freeze count** | A gap between rendered frames longer than max(3×average, average + 150 ms), the same rule WebRTC's stats use. |
| **Audio jitter buffer** | 60 ms target delay, one Opus frame per 20 ms. When a frame is lost, it is rebuilt from the **in-band FEC** in the next packet if that has arrived, otherwise Opus **PLC** extrapolates it. Playout jumps forward if the buffer grows, and restarts with more delay if packets keep arriving after their playout time (adaptive playout). |

## RTCP and measurement

Every second each client sends every peer a **Sender Report** (RTCP 200). It has our NTP-format time, RTP time, and packet and octet counts, plus one **report block** per stream received from that peer:

- **fraction lost** (8-bit) and **cumulative lost** (24-bit), from expected vs received sequence numbers (RFC 3550 A.3)
- **interarrival jitter** J += (|D| − J)/16, where D is the change in transit time (RFC 3550 6.4.1)
- **LSR/DLSR**: middle 32 bits of the peer's last SR time, and how long we held it. The peer computes **RTT = now − LSR − DLSR**.

Every second the client also writes one CSV row per stream (video/audio × in/out). The rows contain bitrate, loss, jitter, RTT, fps, resolution, freezes, audio concealment, controller level and candidate pair. The column layout matches the WebRTC v1 version, so `analysis/plot.py` works for both.

## Rate control (`cc.c`)

There is no browser underneath, so this is the only congestion control in the system. It is AIMD over a 6-level ladder:

| Level | Bitrate | Resolution |
|---|---|---|
| 0 | 1200 kbps | 640×480 |
| 1 | 800 kbps | 640×480 |
| 2 | 500 kbps | 480×360 |
| 3 | 300 kbps | 320×240 |
| 4 | 180 kbps | 320×240 |
| 5 | 100 kbps | 160×120 |

- **Congestion**: loss > 8 %, or queuing delay (RTT − minimum RTT seen) > 120 ms. **Multiplicative decrease**: drop two levels.
- **Clean**: loss < 2 % and queuing delay < 40 ms, three reports in a row. **Additive increase**: climb one level.
- Bitrate changes are applied live (`vpx_codec_enc_config_set`). A resolution change restarts the encoder, which begins with a keyframe.
- The queuing-delay signal is the delay-based part, the same idea as TCP Vegas/BBR and WebRTC's GCC. It backs off when a bottleneck queue starts filling, *before* packets are dropped. `--no-adapt` pins level 0 for comparison.

## Security notes

- The signalling server validates every line: length ≤ 512 bytes (the connection is dropped otherwise), names limited to `[A-Za-z0-9_-]{1,32}`, candidates parsed as IPv4:port, relay only within the sender's room, 4-person cap, and idle sockets closed after 20 s.
- **Media is not encrypted** in this C version. WebRTC (v1) uses DTLS-SRTP. Adding SRTP (for example libsrtp, with keys exchanged over a TLS signalling channel) is the natural next step. This is listed as a limitation in VIVA_NOTES.
- ICE checks carry the peer IDs in `USERNAME`, so a stray STUN packet cannot attach itself to a call. Without message integrity (ICE's HMAC), an on-path attacker could still spoof one.
