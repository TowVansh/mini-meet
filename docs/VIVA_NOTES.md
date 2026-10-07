# Viva Notes

**Why is signalling separate from media?**
WebRTC deliberately leaves signalling undefined. The browser only produces and consumes SDP and ICE candidates. Any channel can carry them; we use WebSocket. Media then goes directly between peers. Keeping the two apart means the server handles a few KB per call, and calls keep running even if the server goes down.

**Why TCP for signalling and UDP for media?**
Signalling must be complete and in order: a lost ICE candidate or a reordered answer breaks setup, so TCP fits. For media, a retransmitted packet that arrives after its playout time is useless. TCP's head-of-line blocking would stall every packet behind one loss. UDP lets the application decide: conceal the loss, send FEC, or NACK and retransmit only when there is still time.

**What is SDP?**
Session Description Protocol: a text format describing codecs, payload types, SSRCs, ICE ufrag/password, the DTLS fingerprint, and the direction of each media section. An offer proposes; the answer selects from it.

**What is ICE?**
Interactive Connectivity Establishment. It gathers candidates (host, srflx, relay), exchanges them through signalling, runs STUN connectivity checks on every candidate pair in both directions, and nominates the best pair that works.

**How does STUN work?**
The client sends a Binding Request over UDP. The server replies with `XOR-MAPPED-ADDRESS`, which is the source IP:port the server saw, meaning the client's address after NAT. That becomes the srflx candidate. The address is XORed so that NAT devices which rewrite IP addresses inside payloads (ALGs) do not corrupt it.

**When does STUN fail? What is TURN?**
Symmetric NAT creates a new mapping for each destination, so the port STUN reported is not the port the peer will see. TURN relays all media through a server with a public IP. It always works, but costs server bandwidth and adds latency.

**What NAT types are there?**
Full cone, restricted cone, port-restricted cone, and symmetric. Hole punching works for all of them except symmetric combined with symmetric or with port-restricted.

**Why mesh, and why cap it at 4?**
Mesh needs no media server, gives one hop of latency, and keeps true end-to-end encryption. But each person uploads N−1 streams and runs N−1 encoders. At 4 people that is 3× uplink. Beyond that an SFU is the right design.

**What is an SFU, and how is it different from an MCU?**
An SFU forwards packets without decoding them; each client uploads one stream and downloads N−1 (often with simulcast layers). An MCU decodes, mixes and re-encodes into one stream, which is CPU-heavy and adds delay.

**How is media secured?**
DTLS handshake on the media path. Each side's certificate fingerprint is in the SDP. SRTP keys are derived from the handshake, so every RTP packet is encrypted and authenticated. Encryption is mandatory in WebRTC. In mesh, the server cannot decrypt media.

**DTLS-SRTP vs IPsec?**
IPsec secures IP packets at layer 3 between hosts or gateways and needs OS or network setup. DTLS-SRTP runs inside the application, per session and per peer, with no OS configuration, and crosses NAT easily because it rides on UDP.

**What is jitter, and how is it handled?**
Variation in packet delay. The receiver's jitter buffer holds packets briefly and plays them at a steady rate. More jitter makes the buffer grow, which adds latency, and packets that arrive too late are treated as lost. Our measurements show this in the jitter30 and jitter60 profiles.

**How is RTT measured?**
RTCP: the sender records when it sent a Sender Report. The receiver's Receiver Report echoes that timestamp (LSR) along with how long it held the report (DLSR). RTT = now − LSR − DLSR. The ICE layer also measures RTT from STUN consent checks (`currentRoundTripTime`).

**What does your adaptation do, and how is it like TCP?**
AIMD. On congestion signals (loss > 8 % or RTT > 400 ms) it cuts quality by two ladder levels at once (multiplicative decrease). After 3 clean seconds it raises quality by one level (additive increase). TCP Reno uses the same shape on its congestion window: it backs off fast so the network recovers, and probes slowly for more.

**What does the browser already do (GCC)?**
Google Congestion Control. The receiver reports per-packet arrival times (transport-wide CC). The sender watches the delay gradient. Queues building up mean congestion before any loss happens, so it lowers its rate. It also uses loss: below 2 % it increases, above 10 % it decreases. Its output is the target bitrate (`availableOutgoingBitrate`).

**Why add your own controller on top of GCC?**
Random loss (for example Wi-Fi) is not congestion, so GCC keeps sending at a high bitrate. Large frames span many packets, and one lost packet breaks the whole frame, causing freezes and keyframe requests. Lowering resolution means fewer packets per frame and fewer broken frames. Compare adapt on vs off in EVALUATION.md.

**How do you handle packet loss?**
Audio: Opus in-band FEC plus packet loss concealment. Video: NACK retransmission when there is time, PLI keyframe requests when decoding breaks, plus our downscaling.

**What is glare, and how did you avoid it?**
Glare is when both peers send an offer at the same time. Our rule: only the newcomer offers, and existing members only answer. (The alternative is WebRTC's "perfect negotiation" pattern with rollback.)

**Why queue ICE candidates?**
Trickle ICE can deliver a candidate before `setRemoteDescription` has run. `addIceCandidate` would then fail, so candidates are held until the remote description is set.

**How did you emulate the network?**
Linux `tc qdisc ... netem` on the loopback interface inside WSL2. It can add random or bursty (Gilbert-Elliott) loss, fixed delay, normally distributed jitter, and a rate limit. Two headless Chromium bots with synthetic media run the call through it, so every run is repeatable.

**Limitations?**
No TURN, so symmetric NAT fails. Mesh does not scale past about 4. The synthetic video is simpler than a real camera feed, so absolute bitrates are lower than a real call. netem on `lo` impairs both directions and all traffic. There is no authentication on rooms.

**Why can't IP multicast carry the conference?**
Multicast is not routed across the public internet (ISPs do not enable inter-domain multicast). So conferencing uses application-level fan-out: mesh or SFU.
