# Study Guide: Mini-Meet in Syllabus Order

How to use this guide: go through the parts in order. For each topic, learn the **theory** bullets, open the **code** listed, and make sure you can answer the **questions**. Topics marked ★ are the core of the project. Expect most viva questions to come from those.

---

## Part 0: Big picture (learn this first)

```
          TCP 9000 (text lines)
   mm A  <----------------->  mm-server  <----------------->  mm B
     \                                                       /
      \==== UDP, peer-to-peer: STUN checks, RTP, RTCP =====/
                     |                 |
                     +--> STUN server <+   (finds public IP:port)
```

- **Two paths.** Signalling (small, must be reliable) goes over TCP through the server. Media (large, time-critical) goes over UDP directly between peers.
- **Call order:** connect TCP → `JOIN` → STUN gets public address → `CAND` exchange → STUN checks punch through NAT → RTP media + RTCP feedback.
- **Programs:** `mm-server` (signalling), `mm-stun` (STUN server), `mm` (client).
- Q: Draw the architecture and explain each arrow. Q: What happens to a running call if the server crashes? (The call keeps going; media doesn't pass through the server.)

---

## Part 1: Module 1, Foundations

### 1.1 Layering and encapsulation
- Media stack: VP8/Opus frame → RTP header → UDP header → IP header → Wi-Fi/Ethernet frame.
- Signalling stack: text line → TCP → IP → link.
- Each layer adds its own header. The receiver strips them in reverse order (demultiplexing).
- Sizes: IP 20 B + UDP 8 B + RTP 12 B + our descriptor 1 B + payload ≤ 1160 B ≈ 1200 B < 1500 MTU.
- Q: Which OSI/TCP-IP layer is RTP in? (Application layer, running on top of UDP. It is sometimes described as "transport-like".)

### 1.2 ★ Sockets API (`src/common/net.c`)
- `socket()` → `bind()` → `listen()` → `accept()` on the server; `socket()` → `connect()` on the client; `send/recv` for TCP; `sendto/recvfrom` for UDP.
- **Non-blocking** sockets: calls return immediately with EWOULDBLOCK instead of waiting.
- **select()**: wait on many sockets at once and report which are readable. Used by the server loop and the client's net thread.
- Windows vs Linux: `WSAStartup`, `closesocket`, `ioctlsocket(FIONBIO)`, `WSAGetLastError` vs `close`, `fcntl(O_NONBLOCK)`, `errno`.
- `htons/ntohl`: **network byte order = big-endian**. `put_u16/put_u32` in `util.h`.
- `TCP_NODELAY` turns off **Nagle's algorithm**, which would hold back small writes to batch them.
- `SO_RCVBUF/SO_SNDBUF`: bigger kernel buffers, so bursts of video packets aren't dropped locally.
- Q: Why select() and not one thread per client? Q: What does bind(port 0) do? (The OS picks a free port.) Q: Why can't a TCP server use `recvfrom`-style framing?

### 1.3 Performance metrics (`netloop.c:stats_tick`, `analysis/plot.py`)
- **Bandwidth** = link capacity. **Throughput** = what you actually get. We log bitrate in kbps every second.
- **Delay components**: propagation + transmission (size/rate) + queuing + processing.
- **Latency**: one-way delay. **RTT**: round trip time.
- **Jitter**: variation in delay.
- Transmission delay example: 1200 B at 1 Mbit/s = 9600 bits / 10⁶ = 9.6 ms.
- Queue delay example (our bufferbloat): 1000 packets × 1200 B × 8 / 250 kbit/s ≈ **38 s** of queue capacity.
- Q: In the rate250k run, why did RTT reach about 20 s? (A huge FIFO queue was being filled faster than the link drained it.)

---

## Part 2: Module 2, Direct links (only what we use)

### 2.1 Framing (`media_in.c:send_video_frame`)
- One VP8 frame (can be 10–50 kB) is split into several RTP packets.
- Boundaries: the `S` bit marks the first packet, the **marker bit** marks the last. All packets of one frame share the same timestamp.
- TCP side: newline framing (`proto.c`).
- Q: How does the receiver know a frame is complete? (Every sequence number from the S packet to the marker packet is present.)

### 2.2 ★ Error detection (sequence numbers)
- 16-bit RTP sequence number, +1 per packet. A gap means loss.
- Wrap-around: `seq_newer(a,b) = (int16_t)(a-b) > 0`.
- Q: After 65535, what comes next, and how do you still compare correctly?

### 2.3 ★ Error correction: FEC (`media_out.c:audio_tick`)
- **Forward error correction**: send redundancy ahead of time, so the receiver can repair without asking.
- Opus in-band FEC: packet N+1 carries a low-bitrate copy of frame N.
- Loss of N with N+1 present → decode N from N+1 with `opus_decode(..., fec=1)`.
- Both lost → **PLC** (packet loss concealment) extrapolates the sound.
- Compare with **Hamming code** (syllabus): both add redundancy. Hamming corrects bit errors inside a block, while Opus FEC recovers a whole lost packet.
- Q: FEC vs retransmission: when is each better? (FEC costs no round trip but always costs bandwidth. Retransmission only costs bandwidth when there is loss, but needs a full RTT.)

### 2.4 ★ Reliable transmission: ARQ (`media_out.c:nack_tick`, `media_in.c:tx_resend`)
- Stop-and-wait: send 1, wait for an ACK. Go-Back-N: resend everything after the loss. **Selective repeat**: resend only what was lost.
- **Our NACK = selective repeat with a deadline.** The receiver lists the missing seqs, and the sender resends only those.
- NACK format: packet ID (16 bits) + bitmask (16 bits), so one entry covers up to 17 losses.
- Resend timer = 1.5 × RTT, at most 4 tries, and we give up once the frame's deadline passes.
- Reordering tolerance: wait about 2 × jitter before the first NACK.
- Retransmissions use **PT 97**, so they don't count as received in the loss statistics.
- Q: Why NACK (negative ACK) and not ACK every packet like TCP? (Far less feedback traffic, and we only care about losses we can still fix in time.)

### 2.5 Wireless / access networks
- Wi-Fi and mobile links lose packets randomly, not just because of congestion. That fools loss-based congestion control (our loss5 result).
- Mobile uplinks have big buffers, so they produce **bufferbloat** (the NAT demo had a 1.15 s RTT spike).

---

## Part 3: Module 3, Internetworking

### 3.1 ★ NAT (`src/client/ice.c`)
- Private ranges: 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16. Our laptop was 172.16.240.143 (private) and 36.255.16.51 (public).
- NAT rewrites the source IP:port of outgoing packets and keeps a mapping table. Incoming packets are allowed only if they match a mapping.
- Types: **full cone** (anyone can send to the mapping), **restricted cone** (only IPs we sent to), **port-restricted** (only the IP:port we sent to), **symmetric** (a new mapping for every destination).
- Hole punching works unless one side is symmetric and the other is symmetric or port-restricted.
- Mappings expire, hence the **keepalive** every 2.5 s.
- Q: Why can't two NATed peers just send to each other's public IP? Q: Why does symmetric NAT break STUN?

### 3.2 ★ STUN (`src/stun/stun.c`)
- Header 20 B: type (16) + length (16) + **magic cookie 0x2112A442** + transaction ID (96 bits).
- Binding request → success response carrying **XOR-MAPPED-ADDRESS** = the source IP:port the server saw.
- XOR with the cookie, so NAT "ALGs" don't rewrite the IP inside the payload.
- Attributes are TLV (type, length, value), padded to 4 bytes.
- We also use STUN for peer connectivity checks, with `USERNAME = "<to>:<from>"`.
- Q: Walk through a STUN request byte by byte. Q: What is the transaction ID for? (Matching a response to its request.)

### 3.3 ★ ICE-style candidates and hole punching
- **host** = own interface IP. **srflx** = public mapping from STUN. **prflx** = learned from an incoming check. **relay** = TURN (we don't have it).
- Both sides send checks at the same time. Each outgoing packet opens our own NAT for the reply.
- The first candidate that answers is selected. Demo result: `srflx->srflx` between college Wi-Fi and mobile hotspot.
- Q: Explain hole punching with a diagram. Q: What is TURN, and when is it needed?

### 3.4 IP addressing and subnetting
- netns bench: 10.10.0.1/24 and 10.10.0.2/24 on a veth pair, with default routes via each other.
- `net_local_ip()`: connect a UDP socket to 8.8.8.8 and getsockname → the interface the routing table picks.
- Q: Why does connect() on a UDP socket send nothing? (UDP is connectionless. connect only sets the default destination and selects the route.)

### 3.5 Fragmentation / MTU
- If a datagram is bigger than the MTU, IP fragments it, and losing any fragment loses the whole datagram.
- That's why payloads are capped at 1160 B.

### 3.6 Multicast (why not)
- IP multicast isn't routed on the public internet, so conferences fan out at the application layer: mesh (us) or SFU.

---

## Part 4: Module 4, End-to-end protocols (the main module)

### 4.1 ★ UDP vs TCP
| | TCP | UDP |
|---|---|---|
| Connection | 3-way handshake | none |
| Reliability | retransmits until delivered | none |
| Order | in order (head-of-line blocking) | any order |
| Boundaries | byte stream | keeps datagram boundaries |
| Use here | signalling | STUN, RTP, RTCP |
- **Head-of-line blocking**: with TCP, one lost packet delays everything behind it by at least one RTT. That's fatal for live video.
- Q: Why not send video over TCP? Q: Why not signalling over UDP?

### 4.2 ★ Demultiplexing (`netloop.c:on_udp`)
- One UDP port carries everything. **RFC 7983**: first byte 0–3 means STUN, 128–191 means RTP/RTCP.
- RTCP vs RTP: second byte 200–206 means RTCP (RFC 5761).
- Then the source address tells us which peer, and the payload type tells us which stream (96 VP8, 97 RTX, 111 Opus).
- UDP's own demultiplexing is by port number. We demultiplex further above it.

### 4.3 ★ TCP connection setup/teardown and record boundaries (`mm_server.c`, `proto.c`)
- 3-way handshake (SYN, SYN-ACK, ACK) happens inside `connect/accept`. Teardown: FIN/ACK, and `recv()` returns 0.
- If the FIN never arrives (crash, tunnel), the **idle timeout** (20 s) detects the dead peer.
- One `recv` may return half a line or several lines, so we keep a **line buffer** that splits on `\n`.
- Q: What if a client sends a 10 kB line? (It's rejected at 512 B and the connection is closed, which protects against DoS.)

### 4.4 ★ RTP (`rtp.c`)
- 12-byte header: V=2 | P | X | CC | M | PT | seq (16) | timestamp (32) | SSRC (32).
- **seq** = packet order and loss detection. **timestamp** = media clock (90 kHz video, 48 kHz audio). **SSRC** = stream ID.
- Draw the header from memory.

### 4.5 ★ RTCP (`rtp.c`, `netloop.c:send_reports/on_rtcp`)
- **SR (200)**: NTP time, RTP time, packets and bytes sent, plus report blocks.
- Report block: SSRC, **fraction lost** (8 bits, ×256), **cumulative lost** (24 bits), extended highest seq, **jitter**, **LSR**, **DLSR**.
- **NACK (205/1)**, **PLI (206/1)**.
- Loss formulas (RFC 3550 A.3):
  - expected = ext_highest − base + 1
  - lost = expected − received
  - fraction = (lost_interval << 8) / expected_interval
- ★ **Jitter**: D = (R_j − R_i) − (S_j − S_i); J = J + (|D| − J)/16
- ★ **RTT** = A − LSR − DLSR (A = arrival time of the report). No clock sync is needed, because A and LSR both come from our own clock.
- Q: Work an RTT example. We sent an SR at t=10.000 s. The peer held it 0.050 s. Its report arrives at t=10.130 s. RTT = 10.130 − 10.000 − 0.050 = **80 ms**.

### 4.6 ★ Adaptive retransmission (timers from RTT)
- TCP: RTO from SRTT and RTTVAR (Jacobson). Ours: NACK resend = 1.5 × RTT, frame wait = max(120 ms, 1.5 × RTT + 40 ms), PLI interval ≥ 1.5 × RTT.
- SRTT in `cc.c`: srtt = 0.75·srtt + 0.25·sample (the same EWMA idea as TCP).

### 4.7 ★ Congestion control (`cc.c`)
- **AIMD** (TCP Reno): additive increase, multiplicative decrease, giving the sawtooth.
- Ours: on loss > 8 % or queuing delay > 120 ms, drop 2 levels. After 3 clean reports (loss < 2 %, queuing delay < 40 ms), climb 1 level.
- Ladder: 1200k@640×480 → 800k → 500k@480×360 → 300k@320×240 → 180k → 100k@160×120.
- **Delay-based**: queuing delay = smoothed RTT − minimum smoothed RTT over the last 10 s (the BBR/Vegas idea). It sees the queue filling **before** loss.
- Windowed minimum: a path that just gets longer (+100 ms) becomes the new baseline after 10 s (bug found and fixed using the delay100 run).
- Weakness: random loss looks like congestion, so the controller drops quality for nothing (loss5 result).
- Q: Why decrease faster than increase? (Stability and fairness: the network recovers quickly, and spare capacity is probed slowly.) Q: Draw the sawtooth from the NAT demo log.

### 4.8 Queuing disciplines and resource allocation
- **FIFO / drop-tail**: what netem `rate` uses (1000-packet queue), which gives bufferbloat.
- **Fair queuing**: a separate queue per flow, served round robin, so one flow can't hog the link.
- **AQM** (RED, CoDel): drop or mark early to keep queues short. Would fix our 20 s RTT.
- Q: What is bufferbloat, and how would CoDel help?

### 4.9 QoS
- Metrics we measure: bitrate, fps, resolution, loss, jitter, RTT, freezes, audio concealment.
- Freeze = a frame gap longer than max(3 × average, average + 150 ms).
- IntServ/DiffServ (theory): DSCP marking (EF for voice). We don't mark packets.

### 4.10 RPC / SCTP (only compare)
- Our signalling is request/response plus events over TCP. RPC hides network calls behind function calls.
- SCTP: message-oriented, multi-stream (no head-of-line blocking between streams). WebRTC data channels use it (our v1).

---

## Part 5: Module 5, Security and applications

### 5.1 Threats and what we do
- DoS through huge lines → 512 B limit. Room hijack → relay only within a room. Bad input → name and IP validation.
- Dead connections → heartbeat timeout. Spoofed STUN → USERNAME check (still no HMAC).
- **Not done: media encryption.** Fix: SRTP (AES-CTR + HMAC-SHA1) with keys from DTLS or TLS signalling.
- Q: What could an attacker on the same Wi-Fi do to your call? (Read or inject RTP, because it isn't encrypted.)

### 5.2 IPsec vs SRTP
- IPsec: layer 3, AH/ESP, tunnel/transport mode, needs OS config, trouble with NAT (NAT-T needed).
- SRTP: application layer, per session, passes NAT easily, encrypts only the RTP payload and leaves the header readable for routing and stats.

### 5.3 Firewalls
- Windows Firewall prompted for `mm.exe` / `mm-server.exe`. An inbound allow rule is needed for the server port.
- Stateful firewalls let replies back in. Hole punching relies on that.

### 5.4 Application protocols to relate to
- **Telnet**: our signalling is plain text, so you can type `JOIN demo bob` by hand in telnet.
- **HTTP**: also text-based request/response (compare with our protocol). v1 used HTTP + WebSocket.
- **DNS**: `getaddrinfo("stun.l.google.com")` before STUN.
- **SNMP vs telemetry**: polling vs push. Our per-second CSV is push telemetry.

---

## Part 6: Module 6
- **Telemetry**: streaming metrics continuously (our stats CSV) vs SNMP polling.
- **NETCONF**: config over XML/SSH. Mention only as industry practice.

---

## Part 7: Results you must remember (from EVALUATION.md)

- 2 peers, clean: 1200 kbps, 640×480, 20 fps, RTT < 1 ms.
- 4-peer mesh: all 12 streams at 640×480, 20 fps.
- Random loss ≤ 5 %: NACK + FEC hide it.
- 1 Mbit/s link without adaptation: RTT ~10 s, ~57 % loss, video stops, audio broken. **With adaptation: smooth 20 fps at about 300–800 kbps.**
- Real NAT demo: college Wi-Fi ↔ mobile hotspot, `srflx->srflx`, RTT median 85 ms, spikes to 1.15 s on the hotspot uplink.

---

## Part 8: Reading the code (in this order)

1. `src/common/proto.h` → `proto.c`: the protocol in 100 lines
2. `src/server/mm_server.c`: select() server, rooms, relay, timeout
3. `src/common/net.c`: the socket wrapper
4. `src/stun/stun.c`: packet building/parsing, XOR address
5. `src/client/ice.c`: gather, checks, hole punching
6. `src/client/rtp.c`: RTP/RTCP formats, loss and jitter math
7. `src/client/netloop.c`: demultiplexing, RTT, stats
8. `src/client/media_out.c`: jitter buffers, NACK, PLI, FEC
9. `src/client/cc.c`: AIMD
10. `src/client/media_in.c`: capture, encode, packetize (codec details are less important)

---

## Part 9: Live demo checklist

1. `make` (or run the release .exe files), then start `mm-server`.
2. Two windows on one PC: `mm --server 127.0.0.1 --room demo --name A` and `... --name B --test`.
3. Point at the overlay: PATH, RTT, LOSS, JITTER, FREEZES, LVL.
4. Optional: `telnet 127.0.0.1 9000`, then type `JOIN demo me` to show the text protocol.
5. Show `results/` graphs and `docs/EVALUATION.md`.
6. Show the NAT demo table (`docs/NAT_DEMO.md`).
