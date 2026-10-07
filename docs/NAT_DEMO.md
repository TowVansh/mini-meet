# NAT Traversal Demo (STUN across separate networks)

Goal: show a call between two devices on **different networks**, where both are behind NAT, and the media path goes directly peer-to-peer using a server-reflexive (`srflx`) candidate learned through STUN.

## Setup

1. **Device 1**: laptop on college or home Wi-Fi, running the server (`npm start` in WSL).
2. **Device 2**: a phone on **mobile data**, or a second laptop using the phone's hotspot. This must not be the same Wi-Fi.
3. Make the signalling server reachable from the internet over HTTPS with a tunnel (no router changes needed):
   ```bash
   # inside WSL, second terminal
   cloudflared tunnel --url http://localhost:8443
   # prints https://<random>.trycloudflare.com
   ```
   (`ngrok http 8443` also works.) Only signalling and the web page go through the tunnel. **Media does not**, because the tunnel carries HTTP/WebSocket only and WebRTC media is UDP.
4. Open the tunnel URL on both devices, join the same room.

## What to show

- The tile overlay line `path srflx->srflx udp`, or `host->srflx` / `srflx->host` when one side has a public IP. This proves ICE picked a NAT-mapped address learned from STUN.
- `chrome://webrtc-internals` (or `edge://webrtc-internals`): open the connection, find the **candidate-pair** with `nominated: true`, and show its local and remote candidates (type `srflx`, the public IP of each network).
- Optional Wireshark capture on the laptop:
  - filter `stun`: Binding Request to `stun.l.google.com:19302` and the reply carrying `XOR-MAPPED-ADDRESS` (the laptop's public IP:port).
  - then STUN connectivity checks sent straight to the phone's public IP: this is the hole punching.
  - filter `udp && !stun`: DTLS handshake followed by SRTP (RTP payloads are encrypted) flowing to the phone's public IP, **not** to the server.

## Expected failure case

If both sides sit behind **symmetric NAT** (common on carrier-grade NAT for mobile data), the connection state goes to `failed` and the browser console logs `connection to <name> failed (NAT traversal without TURN?)`. The port STUN reported is not the port the peer actually sees, so hole punching cannot work. The fix is a TURN relay (for example coturn), which is outside the project scope (STUN only). Record which network pairs worked and which failed, and include that table in EVALUATION.md.

| Device 1 network | Device 2 network | Selected pair | Result |
|---|---|---|---|
| College Wi-Fi | Jio mobile data | | |
| College Wi-Fi | Airtel hotspot | | |
| Home Wi-Fi | Mobile data | | |
| Same Wi-Fi (control) | Same Wi-Fi | host->host | |
