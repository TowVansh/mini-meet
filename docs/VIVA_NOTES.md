# Viva Notes

## Design

**What did you build?**
A C program (`mm`) that makes 2–4 person audio/video calls over raw UDP, a C signalling server (`mm-server`), and a C STUN server (`mm-stun`). It builds on Windows (MinGW) and Linux. We wrote all of the networking ourselves: sockets, signalling protocol, STUN, hole punching, RTP/RTCP, jitter buffers, NACK, rate control. Libraries are used only for camera/mic capture (FFmpeg), codecs (libvpx, libopus) and the window (SDL2).

**Why are signalling and media separate?**
Signalling is small and must be reliable and ordered, so it goes over TCP through a server. Media is large and time-critical and goes directly between peers over UDP. The server handles a few hundred bytes per call and cannot see media, and calls survive a server crash.

**Why TCP for signalling but UDP for media?**
TCP retransmits until data arrives, in order. A lost video packet would block every packet behind it (head-of-line blocking) for a full retransmission round trip. For live media, data that arrives late is useless. UDP lets us choose per packet: retransmit if there is time (NACK), repair from redundancy (Opus FEC), or conceal (PLC, frame skip plus keyframe).

**How do you make Windows and Linux share one codebase?**
`src/common/net.c` hides the differences: `WSAStartup`/`closesocket`/`ioctlsocket(FIONBIO)`/`WSAGetLastError` on Windows, `close`/`fcntl(O_NONBLOCK)`/`errno` on Linux. `FD_SETSIZE` is raised on Windows (default 64). `SIO_UDP_CONNRESET` is disabled, otherwise Windows reports ICMP port-unreachable as a recv error on UDP. Threads and mutexes come from SDL.

**Why does the server use select() rather than threads?**
One thread, one `select()` over all sockets. There are no locks and no races, and it easily handles the scale needed (rooms of 4). It is the classic event-loop server pattern. epoll would scale further on Linux, but select works on both OSes.

**How does TCP framing work in your protocol?**
TCP has no message boundaries: one `recv` can return half a line or three lines. We buffer bytes and split on `\n` (`mm_linebuf_push/pop`). Lines over 512 bytes are rejected, so a client cannot exhaust server memory.

## NAT traversal

**What is NAT, and why is it a problem?**
Many devices share one public IP. The NAT rewrites the source IP:port of outgoing packets and keeps a mapping. Unsolicited incoming packets have no mapping and are dropped, so two NATed peers cannot just send to each other.

**How does STUN work?**
We send a Binding request to the STUN server. It replies with the source IP:port it saw, in `XOR-MAPPED-ADDRESS`. That is our public mapping, the **srflx** candidate. The address is XORed with the magic cookie so that NAT "helpers" which rewrite IP addresses inside payloads do not corrupt it.

**What is hole punching?**
Both peers learn each other's candidates through signalling, then send STUN checks to each other *at the same time*. A's outgoing packet to B's public address creates a mapping in A's NAT. When B's packet arrives, A's NAT treats it as a reply and lets it in. The same happens on B's side. We pick the first candidate that answers.

**When does it fail?**
With **symmetric NAT**, which uses a different public port for each destination. The port the STUN server saw is not the port the peer will see, so checks to it never match. Two symmetric NATs, or symmetric plus port-restricted, need **TURN**, a relay server that forwards all media. TURN always works but costs server bandwidth and adds latency. We detect this case (15 s timeout) and report it.

**NAT types?**
Full cone, restricted cone, port-restricted cone, and symmetric. Hole punching works for every combination except symmetric with symmetric or with port-restricted.

**What are host, srflx and prflx candidates?**
host = our own interface address (works on the same LAN). srflx = public address learned from STUN. prflx = an address we first learned because a peer's check arrived from it (it can differ from srflx if there is another NAT in the path).

**Why keepalives?**
NAT mappings for UDP expire after roughly 30 s to a few minutes of silence. We send a Binding request every 2.5 s, which keeps the hole open even when media is paused.

## Media

**What is in your RTP packets?**
A 12-byte RFC 3550 header: version, marker, payload type, sequence number, timestamp, SSRC. Video adds a 1-byte descriptor (start-of-frame and keyframe flags). Payloads are at most 1160 bytes, so a video frame is split across several packets and the marker flags the last one.

**Why limit packet size?**
To stay under the path MTU (1500 bytes on Ethernet, less on some links). Bigger datagrams get IP-fragmented, and losing any fragment loses the whole datagram. Fragments also often get dropped by NATs and firewalls.

**What do the sequence number and the timestamp each do?**
The sequence number orders packets and reveals loss (gaps). The timestamp gives the media time (90 kHz video, 48 kHz audio): every packet of one video frame shares it, and jitter is computed from it.

**How do you share one UDP port between STUN, RTP and RTCP?**
By the first byte (RFC 7983). STUN has its top two bits 00 plus the magic cookie at offset 4. RTP/RTCP have version 2, so the first byte is 128–191. RTCP packet types 200–206 sit where RTP has marker+PT, and our RTP PTs (96, 97, 111) never fall in that range (RFC 5761).

## Reliability and quality

**How do you detect loss?**
By gaps in RTP sequence numbers (with 16-bit wrap handled by `seq_newer`). The receiver immediately sends a **NACK** for the missing numbers.

**Explain NACK.**
RTCP Generic NACK (PT 205, FMT 1). Each entry holds a packet ID plus a 16-bit bitmask of which of the next 16 are also missing, so one entry can request 17 packets. The sender keeps the last 1024 video packets and resends them. We resend NACKs every 1.5×RTT, at most 4 times, and give up once the frame's deadline has passed. This is **selective-repeat ARQ with a deadline**.

**Why send retransmissions with a different payload type?**
So the receiver can keep them out of its loss and jitter statistics. RTCP then reports the *network's* loss rate, not loss after repair. That is what the rate controller needs, and the same idea as WebRTC's RTX stream.

**What is a PLI?**
Picture Loss Indication (PT 206, FMT 1). VP8 frames reference earlier frames. If a frame is lost for good, later frames would decode with errors. The receiver drops them and asks for a **keyframe**, which needs no references. This is why video "freezes" after heavy loss.

**How do you handle audio loss?**
Opus in-band **FEC**: each packet carries a low-bitrate copy of the previous frame. If frame N is lost but N+1 arrived, we decode N from N+1's FEC data. If both are missing, Opus **PLC** (packet loss concealment) extrapolates the sound. We tell the encoder the measured loss rate so it sizes the FEC.

**What is jitter, and how does your jitter buffer work?**
Jitter is variation in packet delay. Audio: we hold 60 ms (3 frames) and play one frame every 20 ms. If the buffer grows, we jump forward to cap latency. If packets keep arriving after their playout time, we restart with the delay rebuilt (adaptive playout). Video: we decode a frame once all its packets are present, waiting up to max(120 ms, 2.5×RTT) for retransmissions before skipping.

**How is jitter measured?**
RFC 3550: D = (arrival_j − arrival_i) − (ts_j − ts_i), and J += (|D| − J)/16 in RTP units, reported in RTCP. We divide by 90 (video) or 48 (audio) to get milliseconds.

**How is RTT measured without synchronized clocks?**
Our SR carries our time T1. The peer's next report echoes it (LSR) together with how long it held it (DLSR). When the report arrives at time T2: RTT = T2 − LSR − DLSR. Both T1 and T2 come from *our* clock, so the peer's clock never matters.

**Explain your rate controller.**
AIMD over a 6-level ladder from 1200 kbps @ 640×480 down to 100 kbps @ 160×120. On loss > 8 % *or* queuing delay > 120 ms we drop two levels (multiplicative decrease). After three clean reports in a row we climb one level (additive increase). That is the same shape as TCP Reno's congestion window. Queuing delay = RTT − smallest RTT seen. It rises as soon as a bottleneck queue starts filling, before any loss, which is the delay-based idea from TCP Vegas, BBR and WebRTC's GCC.

**What did the experiments show?** See EVALUATION.md. Without adaptation, a 1 Mbit/s link fills netem's queue: RTT climbs to about 10 s, about 45 % of packets are lost, video stops (0 fps) and audio is completely concealed. With adaptation, the controller drops to about 300 kbps and the call stays smooth at 20 fps. Under pure random loss, the controller reacts too (it cannot tell random loss from congestion), giving up resolution that NACK could have protected. That is the classic weakness of loss-based control on wireless links.

**Mesh vs SFU vs MCU?**
Mesh: each client sends N−1 copies, with no media server, lowest latency and end-to-end delivery, but it caps out at about 4 people because of uplink. SFU: each client sends one stream to a server that forwards it (Meet, Zoom), so uplink stays constant. MCU: the server decodes, mixes and re-encodes, which needs the least client bandwidth but the most server CPU and adds delay. We encode once and send the same packets to every peer at the level of the worst receiver; simulcast or SVC would avoid that trade-off.

## Security and limits

**Is it secure?**
The signalling server validates input (length, names, candidate syntax), relays only within a room, caps rooms, and closes idle connections. STUN checks carry peer IDs. **Media is not encrypted** in the C version. WebRTC (our v1 prototype) uses DTLS-SRTP. The fix would be SRTP with keys exchanged over TLS-protected signalling.

**SRTP vs IPsec?**
IPsec encrypts IP packets at layer 3 between hosts or gateways and needs OS or network configuration. SRTP encrypts only the RTP payload, per session, inside the application, and passes through NAT easily.

**Limitations?**
No TURN (symmetric NAT fails), no media encryption, mesh only (up to 4), one encoder for all peers, IPv4 only, and the test pattern is not a real camera feed (the bench uses it so runs are repeatable). netem on `lo` impairs both directions at once.

**Why not IP multicast for the conference?**
Multicast is not routed across the public internet, so fan-out happens at the application layer: mesh or SFU.
