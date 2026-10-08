# NAT Traversal Demo (two different networks)

Goal: a call between two PCs on **different networks**, both behind NAT, where the media goes directly between them using the public addresses learned through STUN (`srflx`).

## Setup

- **PC 1** (your laptop): college or home Wi-Fi. It runs the signalling server and a client.
- **PC 2** (a teammate's laptop): a phone hotspot or home broadband, anything that is not the same network. It runs only `mm.exe`. On Windows it needs no install: unzip the release zip (`make dist` output, including DLLs).

The signalling server has to be reachable from PC 2. Without access to the router, use a free raw-TCP tunnel:

```bash
bin/mm-server                    # on PC 1
bore local 9000 --to bore.pub    # prints e.g. "listening at bore.pub:41234"
```

Only the signalling (TCP) goes through the tunnel. **Media does not**: it is UDP between the two PCs directly.

## Run

```bash
# PC 1
bin/mm --server 127.0.0.1 --room nat --name Laptop --csv nat-laptop.csv --run nat
# PC 2
mm.exe --server bore.pub:41234 --room nat --name Friend --csv nat-friend.csv --run nat
```

## What to show

- Console: `srflx candidate <public-ip>:<port> (via STUN ...)` on both sides, then `CONNECTED via srflx->srflx`.
- The stats overlay on the remote tile: `PATH SRFLX->SRFLX UDP`, RTT, loss.
- Wireshark on PC 1:
  - filter `stun`: the Binding request to the STUN server and the success reply with XOR-MAPPED-ADDRESS, then Binding requests sent straight to PC 2's public address. That is the hole punching.
  - filter `rtp || rtcp` (Decode As → RTP for the UDP port): RTP to PC 2's public IP, **not** to the server.
- The CSV `candidate_type` column.

## Expected failure case

If both sides are behind **symmetric NAT** (common on mobile carrier-grade NAT), no pair answers. After 15 s the client logs that a TURN relay would be needed, and the tile shows `ICE FAILED (NEEDS TURN)`. Record it as a result: it shows why TURN exists.

## Results

Live run on 2026-10-08: signalling through `bore.pub`, STUN via `stun.l.google.com`. Data is in `results/nat_c__Laptop.csv`.

| PC 1 network | PC 2 network | Candidates | Selected pair | Result |
|---|---|---|---|---|
| Same PC, two clients (control) | — | host | host->host | Connected, RTT < 1 ms |
| Laptop on college Wi-Fi (private 172.16.240.143, public 36.255.16.51) | Second laptop on phone hotspot / mobile data (private 10.129.165.x, public 152.57.86.31) | host + srflx on both | **srflx->srflx** (all 1404 samples) | Connected 3 times, ICE took 99–752 ms each time. Call lasted about 9 minutes in total. |

Both laptops were behind NAT on different ISPs. The media flowed directly between the two public mappings that STUN discovered, and nothing was relayed. Neither NAT was symmetric.

Call quality on this real path:

| | Median | Notes |
|---|---|---|
| RTT | 85 ms | 90th percentile **1.15 s**: the mobile uplink buffered heavily in bursts (bufferbloat) |
| Packet loss | < 0.6 % | |
| Video received | ~230 kbps, 20 fps, 320×240 (median) | The controller averaged level 3.7. It dropped to 160×120 during each RTT spike and climbed back to 480×360 within ~10 s when the queue drained (AIMD sawtooth, see the `[cc]` log lines) |
| Audio | 16.8 % of frames concealed on average | Almost all of it during the RTT spikes, when packets arrived after their playout time |

The server log also shows the heartbeat working. When one client window was closed, the tunnel kept the TCP connection half-open, so no FIN arrived. The server dropped the silent client after its idle timeout and sent `PEER_LEFT` to the room.
