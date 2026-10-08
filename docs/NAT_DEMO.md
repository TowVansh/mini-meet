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

| PC 1 network | PC 2 network | Selected pair | Result |
|---|---|---|---|
| Same PC, two clients (control) | — | host->host | Connected (RTT < 1 ms) |
| | | | |

The WebRTC prototype's run on 2026-10-07 (laptop on college Wi-Fi, phone on mobile data) connected `srflx->srflx`. It used the same STUN-and-hole-punching method, so those two NATs are known to be traversable.
