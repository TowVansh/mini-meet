# Syllabus Coverage

The project targets CO4 and CO5 (Modules 4 and 5). It also uses topics from Modules 1–3 and 6. Every networking mechanism below is our own C code, with the file named.

## Module 4: End-to-End Protocols (CO4)

| Topic | Where in Mini-Meet |
|---|---|
| UDP (simple demultiplexer) | All media on one UDP socket. `netloop.c:on_udp` demultiplexes STUN / RTCP / RTP by first byte (RFC 7983), then by source address and payload type. |
| TCP (reliable byte stream) | Signalling over TCP. `proto.c:mm_linebuf_*` rebuilds message boundaries from the byte stream ("record boundaries"). |
| Connection establishment / termination | TCP `connect`/`accept` in `net.c`. `JOIN`/`LEAVE`. The server drops idle clients after 20 s (dead-peer detection). ICE checks act as a connectivity "handshake" on UDP. |
| Sliding window / triggering transmission | Contrasted with RTP: there is no window; sending is paced by the encoder (20 fps, 20 ms audio). Nagle disabled with `TCP_NODELAY` for signalling (`net.c`). |
| Adaptive retransmission | NACK resend timer = 1.5 × measured RTT; frame wait = max(120 ms, 2.5 × RTT + 30 ms) (`media_out.c`). RTT measured with RTCP LSR/DLSR (`netloop.c:on_rtcp`). |
| Record boundaries | Newline framing over TCP. RTP marker bit + `S` flag mark video frame boundaries across packets. |
| Alternative design choices (SCTP) | Discussed: WebRTC data channels use SCTP over DTLS over UDP (v1 prototype). |
| RPC | The signalling protocol is a small request/response and event protocol (VIVA_NOTES compares it with RPC). |
| Resource allocation, FIFO, fair queuing | netem `rate` = FIFO bottleneck with a 1000-packet queue: our bufferbloat results. Mesh splits uplink between N−1 copies. |
| TCP congestion control (AIMD) | `cc.c`: multiplicative decrease (two levels) on loss/queuing delay, additive increase (one level) after 3 clean reports. |
| Advanced congestion control | Delay-based signal (RTT − minimum RTT), the same idea as TCP Vegas, BBR and WebRTC GCC. |
| Quality of Service | Per-second bitrate, fps, resolution, loss, jitter, RTT, freezes, audio concealment under 14 impairment profiles (EVALUATION.md). |

## Module 5: End-to-End Data and Applications (CO5)

| Topic | Where |
|---|---|
| Security attacks, trust, threats | Server input validation, line-length limit (DoS), relay only within a room, USERNAME-checked STUN. Media encryption is listed as a limitation (ARCHITECTURE.md security notes). |
| IPsec (comparison) | VIVA_NOTES: IPsec at layer 3 vs SRTP at the application layer. |
| Firewalls | NAT/firewall traversal with STUN hole punching. Windows Firewall rules for `mm.exe`. Symmetric NAT needs TURN. |
| Telnet | The signalling protocol is plain text, so it can be driven by hand with `telnet server 9000`. |
| DNS | STUN server and tunnel host resolved with `getaddrinfo` (`net.c:net_resolve`). |
| SNMP / telemetry (Module 6) | Per-second stats CSV = an application telemetry stream (push), compared with SNMP polling. |
| SDN | Not used. Mentioned as how an SFU-based service could steer media. |

## Supporting topics

| Module | Topic | Where |
|---|---|---|
| 1 | Sockets API | `net.c`: one wrapper over Winsock and BSD sockets (`WSAStartup`, `socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`, `select`, non-blocking mode). |
| 1 | Layering, encapsulation | VP8/Opus → RTP → UDP → IP. Signalling text → TCP → IP. |
| 1 | Throughput, delay, jitter | Measured and graphed. RFC 3550 jitter formula in `rtp.c`. |
| 1 | Network byte order | `put_u16/put_u32`, `htons/ntohl` everywhere in packet code. |
| 2 | Error detection | Sequence-number gap detection. STUN magic cookie and length checks. |
| 2 | Error correction (FEC) | Opus in-band FEC rebuilds a lost audio frame from the next packet (`media_out.c:audio_tick`). |
| 2 | ARQ / selective repeat | NACK = selective-repeat ARQ with a deadline. A bitmask acknowledges up to 17 losses per FCI. |
| 2 | Framing | MTU-sized RTP fragments of video frames. Start/marker flags. |
| 3 | NAT, private vs public addresses | host vs srflx candidates. XOR-MAPPED-ADDRESS. NAT types. |
| 3 | IP fragmentation / MTU | Payload cap of 1160 bytes keeps datagrams under the MTU. |
| 3 | Multicast | Why conferencing fans out at the application layer (mesh/SFU) instead of IP multicast. |
