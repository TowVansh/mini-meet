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
| `LEAVE` | Leave the room. Closing the socket does the same. |
| `PING` | Keep-alive, sent every 5 s |

### Server → client

| Message | Meaning |
|---|---|
| `JOINED <self-id> <room> [<id>:<name> ...]` | Join accepted. Lists members who were already present. |
| `PEER_JOINED <id> <name>` | Someone entered your room |
| `PEER_LEFT <id>` | Someone left, disconnected, or timed out |
| `CAND <from-id> <type> <ipv4> <port>` | A peer's candidate, relayed (`to` replaced by `from`) |
| `PONG` | Reply to `PING` |
| `ERROR <code> <text>` | Request rejected |

Error codes: `BAD_MESSAGE`, `BAD_NAME`, `TOO_LARGE`, `ROOM_FULL`, `NOT_JOINED`, `ALREADY_JOINED`, `UNKNOWN_PEER` (the target is not in your room, so relaying across rooms is impossible).

### Liveness

The server closes any connection that has sent nothing for **20 s**. Clients PING every 5 s, so only dead clients (lost network, crashed process) time out, and their peers get `PEER_LEFT`.

### Call setup sequence (C joins a room holding A)

```
 C                         mm-server                        A
 |-- JOIN demo carol ------->|                               |
 |<- JOINED c1 demo a1:alice-|-- PEER_JOINED c1 carol ------>|
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

### RTCP

| Type | Sent | Contents |
|---|---|---|
| SR (200) | Every 1 s to each peer | NTP time, RTP time, packets/octets sent, plus a report block per stream received from that peer |
| Report block | inside SR | SSRC, fraction lost, cumulative lost, extended highest seq, jitter, LSR, DLSR |
| Generic NACK (205, FMT 1) | When sequence gaps are detected | Media SSRC, then FCIs (16-bit packet ID + 16-bit bitmask of the next 16) |
| PLI (206, FMT 1) | When the decoder needs a keyframe | Media SSRC |

**RTT** = arrival time of the report − LSR − DLSR, in units of 1/65536 s. The sender computes it from its own SR timestamps, so the two clocks never need to be synchronized.
