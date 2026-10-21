# Mini-Meet Protocols (v1)

Mini-Meet uses two protocols: a **signalling protocol** over TCP between clients and `mm-server`, and a **media profile** over UDP directly between clients.

---

## 1. Signalling protocol (TCP)

### Framing

- TCP to `mm-server`, port 9000 by default.
- TCP is a byte stream with no message boundaries, so every message is **one line of ASCII ending in `\n`** (a trailing `\r` is ignored). The receiver buffers bytes until a newline arrives (`mm_linebuf_*` in `src/common/proto.c`).
- A line is at most **512 bytes**. A longer line gets `ERROR TOO_LARGE` and the connection is closed.
- Tokens are separated by spaces. The first token is the command.
- Because it is plain text, the protocol can be tested by hand with `telnet localhost 9000` or `nc localhost 9000`.

### Identifiers

- **room**, **name**: 1–32 characters of `A-Z a-z 0-9 _ -`.
- **id**: 8 hex characters, assigned by the server on connect.
- **Room capacity**: 4.

### Client → server

| Command | Meaning |
|---|---|
| `JOIN <room> <name>` | Enter a room (once per connection) |
| `CAND <to-id> <host\|srflx> <ipv4> <port>` | Send one of my network candidates to a peer in my room |
| `KEY <to-id> <64 hex>` | My X25519 public key for end-to-end media encryption |
| `LEAVE` | Leave the room. Closing the socket does the same. |
| `PING` | Keep-alive, sent every 5 s |

### Server → client

| Message | Meaning |
|---|---|
| `JOINED <self-id> <room> [<id>:<name> ...]` | Join accepted. Lists members who were already present. |
| `PEER_JOINED <id> <name>` | Someone entered your room |
| `PEER_LEFT <id>` | Someone left, disconnected, or timed out |
| `CAND <from-id> <type> <ipv4> <port>` | A peer's candidate, relayed (`to` replaced by `from`) |
| `KEY <from-id> <64 hex>` | A peer's public key, relayed. The server never sees a secret key, so it cannot decrypt media. |
| `PONG` | Reply to `PING` |
| `ERROR <code> <text>` | Request rejected |

Error codes: `BAD_MESSAGE`, `BAD_NAME`, `TOO_LARGE`, `ROOM_FULL`, `NOT_JOINED`, `ALREADY_JOINED`, `UNKNOWN_PEER` (the target is not in your room, so relaying across rooms is impossible).

### Liveness

The server closes any connection that has sent nothing for **20 s**. Clients PING every 5 s, so only dead clients (lost network, crashed process) time out, and their peers get `PEER_LEFT`.

A client does not trust `PEER_LEFT` alone. If media from that peer is still arriving, its PINGs were probably just stuck in the same congested queue, so the client keeps the peer until its packets stop for 3 s. Media liveness also works without the server: a connected peer that sends nothing for 30 s is removed. STUN keepalives answer every 2.5 s, so an idle but alive peer is never removed. (Found in testing: under 15 s of bufferbloat the server timed a peer out and the call was torn down although the network had recovered.)

### Call setup sequence (PC3 joins a room holding PC1)

```
 PC3                       mm-server                        PC1
 |-- JOIN demo pc3 --------->|                               |
 |<- JOINED c1 demo a1:pc1 --|-- PEER_JOINED c1 pc3 -------->|
 |-- CAND a1 host 192.168.1.9 50000 -->|-- CAND c1 host ... ->|
 |-- CAND a1 srflx 49.36.1.2 50000 --->|-- CAND c1 srflx ...->|
 |<- CAND a1 host ... --------|<-- CAND c1 host 10.0.0.5 41000|
 |<- CAND a1 srflx ... -------|<-- CAND c1 srflx 106.2.3.4 41000
 |                                                           |
 |==== STUN Binding checks to every candidate, both ways =====|   (hole punching)
 |<=== first answered pair selected ========================>|
 |==== RTP/RTCP media, peer-to-peer over UDP =================|
```

There is no offer/answer and so no "glare" problem. Codecs are fixed (VP8 + Opus), so the only thing that has to be negotiated is the network path, and both sides send their candidates as soon as they learn about each other.

---

## 2. Media profile (UDP, peer-to-peer)

STUN, RTP and RTCP share **one UDP socket**, demultiplexed by the first byte (RFC 7983): `0–3` STUN, `128–191` RTP/RTCP. RTCP is recognized by packet type 200–206 in the second byte (RFC 5761).

### STUN (RFC 5389 subset)

| Use | Message | Attributes |
|---|---|---|
| NAT discovery | Binding request to STUN server | none |
| | Binding success from server | `XOR-MAPPED-ADDRESS` |
| Connectivity check / keepalive | Binding request to peer candidate | `USERNAME = "<their-id>:<my-id>"` |
| | Binding success from peer | `XOR-MAPPED-ADDRESS` (where the request came from) |

### RTP (RFC 3550)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|V=2|P|X|  CC   |M|     PT      |       sequence number         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                             SSRC                              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| PT | Payload | Clock | Notes |
|---|---|---|---|
| 96 | VP8 | 90 kHz | 1-byte descriptor first: bit 7 `S` = start of frame, bit 6 `K` = keyframe. `M` = last packet of the frame. Payload ≤ 1160 bytes. |
| 97 | VP8 retransmission | 90 kHz | Same bytes as the original (same seq), only the PT differs |
| 111 | Opus | 48 kHz | One 20 ms frame per packet, mono, 32 kbps, in-band FEC |

**Field rules (sender)**

- **SSRC**: random 32-bit value per stream (video and audio each have their own), chosen at start-up.
- **Sequence number**: starts at a random value and goes up by 1 for every packet of that stream (wraps at 65535). Retransmissions reuse the original number.
- **Timestamp**: media time, not send time. Video uses a 90 kHz clock taken at capture, and every packet of one frame shares it. Audio adds 960 (20 ms at 48 kHz) per packet, so a missing packet's timestamp can be worked out from its sequence number.
- **Framing**: a video frame is split into payloads of at most 1160 bytes. The first carries `S`, the last carries the marker bit `M`, so the receiver can tell when a frame is complete.

### Receiver rules: jitter buffer and loss concealment

| Step | Rule (`src/client/media_out.c`, playout clock in `rtp.c`) |
|---|---|
| Loss detection | A gap in sequence numbers marks the missing packets. After a reordering wait (about 2 × jitter, 5–80 ms) they are NACKed. |
| Playout clock | For each packet, offset = arrival time − media time (timestamp in µs). The **95th percentile** of the last 64 offsets says when media time T is due: T + p95. The buffer delay therefore grows with network jitter and shrinks when it calms down. |
| Video playout | A frame is decoded only when complete **and** due, so frames are shown at the sender's 20 fps spacing instead of the network's uneven arrival times. |
| Video concealment | If a hole is not repaired within max(120 ms, 1.5 × RTT + 40 ms), the frame is skipped and a keyframe is requested (PLI). Until it arrives the last good picture stays on screen (a freeze). |
| Audio playout | One 20 ms frame is due every 20 ms of media time, plus a 25 ms margin. |
| Audio concealment | A frame that is missing when due is rebuilt from the next packet's in-band **FEC** if that has arrived, otherwise Opus **PLC** extrapolates it. A packet that arrives after its frame was played is dropped as late (but still updates the playout clock). |
| Measured | Per second, in each client's CSV: `playout_sd_ms` (spread of the intervals between shown frames = playout smoothness), `jb_delay_ms` (delay the buffer adds), `freeze_count`, `concealed_pct`. |

`--no-jb` turns the jitter allowance off: playout then follows the fastest packet, which is used to measure what the buffer buys (EVALUATION.md).

### RTCP

| Type | Sent | Contents |
|---|---|---|
| SR (200) | Every 1 s to each peer | NTP time, RTP time, packets/octets sent, plus a report block per stream received from that peer |
| Report block | inside SR | SSRC, fraction lost, cumulative lost, extended highest seq, jitter, LSR, DLSR |
| Generic NACK (205, FMT 1) | When sequence gaps are detected | Media SSRC, then FCIs (16-bit packet ID + 16-bit bitmask of the next 16) |
| PLI (206, FMT 1) | When the decoder needs a keyframe | Media SSRC |

**RTT** = arrival time of the report − LSR − DLSR, in units of 1/65536 s. The sender computes it from its own SR timestamps, so the two clocks never need to be synchronized.

### Encryption (default on, `--no-encrypt` to disable)

| Packet | Clear (authenticated) | Encrypted | Appended |
|---|---|---|---|
| RTP | 12-byte header | payload | 16-byte Poly1305 tag |
| RTCP | first 8 bytes | rest of compound packet | 16-byte tag + 4-byte index |
| STUN | everything | — | — |

Keys come from X25519 (`KEY` lines), one per direction. AEAD = ChaCha20-Poly1305 (IETF). RTP nonce = SSRC ‖ seq ‖ PT ‖ timestamp ‖ 0x00. RTCP nonce = index ‖ 0… ‖ 0x01. Details: `src/client/crypto.h`.
