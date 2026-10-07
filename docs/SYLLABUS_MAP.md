# Syllabus Coverage

The project targets CO4 and CO5 (Modules 4 and 5). It also touches topics from Modules 1–3 and 6, listed here as supporting material for the viva.

## Module 4: End-to-End Protocols (CO4)

| Topic | Where it appears in Mini-Meet |
|---|---|
| UDP (simple demultiplexer) | All media is RTP/SRTP over UDP. BUNDLE and rtcp-mux put every stream on one port; demultiplexing happens by SSRC and payload type. |
| TCP (reliable byte stream) | Signalling runs over WebSocket on TCP: ordered, reliable delivery of SDP and ICE messages. ARCHITECTURE.md explains why each path uses its transport. |
| Connection establishment / termination | Offer/answer and ICE checks compared with TCP's 3-way handshake. The `leave` message and the heartbeat timeout handle teardown. |
| Sliding window, triggering transmission | Contrasted with RTP, which has no window: sending is paced by the encoder and the pacer. NACK-based selective retransmission is the closest analogue. |
| Adaptive retransmission | RTCP-derived RTT is measured and logged. NACK retransmits only if the packet can still arrive before its playout deadline. |
| Record boundaries | WebSocket frames keep message boundaries over a TCP byte stream. RTP keeps frame boundaries with the marker bit. |
| TCP extensions / SCTP | WebRTC data channels run SCTP over DTLS over UDP (mentioned as an alternative design choice). |
| RPC | The signalling protocol is a small request/response and event protocol over WebSocket. Compared with RPC in VIVA_NOTES. |
| Resource allocation, FIFO, fair queuing | netem's `rate` profile is a FIFO bottleneck. Mesh topology splits uplink capacity between N−1 streams. |
| TCP congestion control (AIMD) | `client/adapt.js` is an AIMD controller: drop 2 levels on loss or high RTT, climb 1 level after 3 clean seconds. |
| Advanced congestion control | Google Congestion Control (delay-gradient bandwidth estimation) inside the browser. Its estimate is logged as `avail_out_kbps`. |
| Quality of Service | Measured QoS/QoE metrics: bitrate, fps, resolution, loss, jitter, RTT, freezes, audio concealment, under 14 impairment profiles. |

## Module 5: End-to-End Data and Applications (CO5)

| Topic | Where it appears |
|---|---|
| Security attacks, trust and threats | Threat notes in ARCHITECTURE.md: signalling MITM, room hijack, DoS through large messages. Defences: input validation, 64 KiB size limit, relay only within a room. |
| IPsec (comparison) | DTLS-SRTP gives end-to-end media encryption at the application layer, while IPsec works at the network layer. Compared in VIVA_NOTES. |
| Firewalls | NAT/firewall traversal with ICE + STUN. Explains why symmetric NAT and UDP-blocking firewalls need TURN (NAT_DEMO.md). |
| HTTP / WWW | Express serves the client over HTTP(S). The WebSocket upgrade starts as HTTP/1.1 `Upgrade`. Stats are uploaded with HTTP POST. |
| DNS | STUN server reached by hostname. The tunnel URL is resolved by DNS. Chrome hides host IPs behind mDNS `.local` names (the bots disable this). |
| SNMP / telemetry (Module 6) | The `getStats()` → CSV pipeline is an application telemetry stream, the same idea as streaming telemetry replacing SNMP polling. |
| SDN | Not used directly. Mentioned in VIVA_NOTES as how an SFU-based service could steer media. |

## Supporting topics from Modules 1–3

| Module | Topic | Where |
|---|---|---|
| 1 | Layering and encapsulation | Media stack: Opus/VP8 → RTP → SRTP → (DTLS keys) → UDP → IP. Signalling stack: JSON → WebSocket → TLS → TCP → IP. |
| 1 | Sockets API | `ws` server sockets on Node. Browsers open UDP sockets for ICE. |
| 1 | Throughput, delay, latency, jitter | All measured per second and graphed (EVALUATION.md). |
| 2 | Error detection / correction | Opus in-band FEC, packet-loss concealment, NACK retransmission, keyframe requests (PLI). |
| 2 | Sliding window / ARQ | NACK-based selective repeat compared with stop-and-wait and Go-Back-N. |
| 3 | NAT, ICMP, addressing | Core of the NAT traversal demo: private vs public addresses, NAT mapping types. |
| 3 | IPv6 | ICE gathers IPv6 candidates too, where the network has IPv6. |
| 3 | Multicast | Why the internet cannot multicast conference media, which is why mesh or an SFU is used (VIVA_NOTES). |
