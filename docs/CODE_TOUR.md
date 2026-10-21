# Code Tour: Where to Point When Asked

Open the file, jump to the line, explain the function. Grouped by the questions reviewers usually ask.

## "Show me the socket programming"
| What | Where |
|---|---|
| Windows vs Linux socket layer | `src/common/net.c:7` `net_init` (WSAStartup), `:22` `sock_close`, `:31` `sock_set_nonblock` |
| TCP server socket: socket/bind/listen | `src/common/net.c:128` `tcp_listen` |
| TCP client: connect, Nagle off | `src/common/net.c:145` `tcp_connect` |
| UDP socket, buffer sizes, Windows ICMP quirk | `src/common/net.c:163` `udp_bind` |
| sendto / recvfrom | `src/common/net.c:191` `udp_send`, `:197` `udp_recv` |
| Finding our LAN IP (UDP connect trick) | `src/common/net.c:107` `net_local_ip` |
| Network byte order | `src/common/util.h` `put_u16/put_u32/get_u16/get_u32` |

## "Show me the server"
| What | Where |
|---|---|
| select() event loop, accept, recv, timeouts | `src/server/mm_server.c:172` `main` |
| JOIN handling, room cap, JOINED/PEER_JOINED | `src/server/mm_server.c:78` `handle_join` |
| Relaying candidates / keys only within a room | `:107` `handle_cand`, `:120` `handle_key` |
| Peer leaves → PEER_LEFT | `:67` `leave_room` |
| TCP framing (byte stream → lines) | `src/common/proto.c:41` `mm_linebuf_push`, `:50` `mm_linebuf_pop` |

## "Show me NAT traversal / STUN"
| What | Where |
|---|---|
| STUN packet build (header, magic cookie, TLV) | `src/stun/stun.c:41` `stun_build_request`, `:50` `stun_build_success` (XOR address) |
| STUN parse | `src/stun/stun.c:68` `stun_parse` |
| Getting the public address (srflx) | `src/client/ice.c:21` `ice_gather` |
| Hole punching checks every 100 ms, timeout, keepalive | `src/client/ice.c:112` `ice_tick` |
| Answering checks, picking the pair | `src/client/ice.c:143` `ice_on_stun` |
| Our own STUN server | `src/stun/mm_stun.c` |

## "Show me the media path"
| What | Where |
|---|---|
| RTP header write/parse | `src/client/rtp.c:7`, `:16` |
| Splitting a video frame into packets (MTU) | `src/client/media_in.c:40` `send_video_frame` |
| Demux STUN / RTCP / RTP on one port | `src/client/netloop.c:250` `on_udp` |
| VP8 encoder setup (bitrate, keyframes) | `src/client/media_in.c:293` `venc_open` |
| Opus with in-band FEC | `src/client/media_in.c:421` `audio_thread` |
| Streaming a recorded file in real time | `src/client/media_in.c` `pace_file_frame` (used by `av_next`) |

## "Show me loss handling"
| What | Where |
|---|---|
| Gap detection → NACK list | `src/client/media_out.c:59` `rx_video` |
| NACK timer, reorder tolerance, give-up | `src/client/media_out.c:248` `nack_tick` |
| NACK packet format (PID + bitmask) | `src/client/rtp.c:73` `rtcp_write_nack` |
| Retransmission from history (PT 97) | `src/client/media_in.c:74` `tx_resend` |
| Frame assembly, deadline, skip + PLI | `src/client/media_out.c:160` `video_tick` |
| Audio jitter buffer, FEC, PLC | `src/client/media_out.c:341` `audio_tick` |
| Sequence wrap-around compare | `src/common/util.h:32` `seq_newer` |

## "Show me the jitter buffer"
| What | Where |
|---|---|
| Playout clock: offsets, 95th percentile, due time | `src/client/rtp.c` `playout_add`, `playout_due_us` |
| Video: hold a complete frame until it is due | `src/client/media_out.c` `video_tick` (first `if` in the loop) |
| Audio: one frame due per 20 ms, FEC/PLC, late packets | `src/client/media_out.c` `rx_audio`, `audio_tick` |
| Playout smoothness metric (frame-interval spread) | `src/client/media_out.c` `show_frame` → `src/client/netloop.c` `stats_tick` (`playout_sd_ms`) |
| Buffer on/off switch for the comparison | `--no-jb` in `src/client/main.c` (`min_only` in `playout_t`) |
| Changing network conditions in one call | `bench/run-matrix.sh` (`SCHEDULE=...`) |

## "Show me the measurements"
| What | Where |
|---|---|
| Loss % and jitter formula (RFC 3550) | `src/client/rtp.c:182` `rx_stats_update` (line 202: J += (\|D\|−J)/16), `:258` `rx_stats_block` |
| Sender Report with report blocks | `src/client/rtp.c:56` `rtcp_write_sr` |
| RTT = now − LSR − DLSR | `src/client/netloop.c:144` in `on_rtcp` (line 125) |
| Per-second CSV stats | `src/client/netloop.c:204` `stats_tick` |

## "Show me congestion control"
| What | Where |
|---|---|
| AIMD + slow start + delay signal | `src/client/cc.c:42` `cc_on_report` |
| Queuing delay = SRTT − windowed min | `src/client/cc.c:28` `cc_queuing_delay` |
| Bitrate/resolution ladder | `src/client/cc.c` `CC_LADDER` |
| Applying the level to the encoder (worst peer) | `src/client/netloop.c` `apply_cc` + `check_feedback` (feedback starvation) |

## "Show me security"
| What | Where |
|---|---|
| X25519 key agreement + safety code | `src/client/crypto.c:30` `mmc_derive` |
| Encrypt/decrypt RTP (header = associated data) | `src/client/crypto.c:60` `mmc_seal_rtp`, `:69` `mmc_open_rtp` |
| Nonce construction | `src/client/crypto.c:52` `rtp_nonce` |
| Dropping forged packets | `src/client/netloop.c:250` `on_udp` (AUTH-FAIL) |
| Server input validation | `src/common/proto.c` `mm_valid_name`, `mm_valid_key_hex`; `mm_server.c` 512-byte limit in `main` |

## "Does it work?" (run live)
```
make test                                   # 72 unit-test checks: STUN, RTP/RTCP, stats math, jitter buffer, AIMD, crypto, tamper
bin\mm-server.exe
bin\mm.exe --server 127.0.0.1 --room demo --name PC1
bin\mm.exe --server 127.0.0.1 --room demo --name PC2 --test
```
Point at the overlay: PATH, RTT, LOSS, JITTER, E2E safety code (same on both windows).
