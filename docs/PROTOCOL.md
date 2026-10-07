# Mini-Meet Signalling Protocol (v1)

## Transport

- WebSocket at path `/ws` on the same host and port as the web client (`ws://` on localhost, `wss://` with TLS).
- Each message is one JSON object in one text frame. Every message has a `type` field.
- Maximum message size is 64 KiB. Larger messages get `error TOO_LARGE`.
- Liveness: the server sends a WebSocket ping every 15 s. A socket that has not answered the previous ping is terminated, and its peers receive `peer-left`. Clients can also send an application-level `ping` and get back a `pong`.

The server only handles signalling. Audio and video go directly between browsers over SRTP/UDP and never pass through the server.

## Identifiers

- **room**: 1–32 characters from `A-Z a-z 0-9 _ -`. A room is created on first join and deleted when its last member leaves.
- **peer id**: 8 hex characters. The server assigns it on `join`. Clients address each other only by peer id.
- **Room capacity**: 4 peers.

## Client → Server

| type | fields | meaning |
|---|---|---|
| `join` | `room`, `name` | Enter a room. Allowed once per socket. `name` is trimmed to 32 chars. |
| `offer` | `to`, `sdp` | SDP offer for peer `to`. |
| `answer` | `to`, `sdp` | SDP answer for peer `to`. |
| `ice` | `to`, `candidate` | ICE candidate (`RTCIceCandidateInit`) for `to`. `null` means end of candidates. |
| `leave` | — | Leave the room. Closing the socket does the same. |
| `ping` | — | Application keep-alive. |

## Server → Client

| type | fields | meaning |
|---|---|---|
| `joined` | `protocol`, `selfId`, `room`, `peers[{id,name}]` | Join accepted. `peers` lists members who were already there. |
| `peer-joined` | `peer{id,name}` | Another peer entered your room. |
| `peer-left` | `id` | A peer left, closed its socket, or timed out. |
| `offer` / `answer` / `ice` | `from` + same payload | Relayed message. `to` is replaced by `from`. |
| `pong` | `t` | Reply to `ping` (server time, ms). |
| `error` | `code`, `message` | Request rejected. |

### Error codes

`BAD_JSON`, `BAD_MESSAGE` (unknown type or missing/wrong-typed field), `TOO_LARGE`, `BAD_ROOM`, `ROOM_FULL`, `NOT_JOINED`, `ALREADY_JOINED`, `UNKNOWN_PEER` (target is not in your room, so relaying across rooms is impossible).

## Call setup rule (glare avoidance)

The **newcomer** sends an `offer` to every peer listed in `joined.peers`. Existing members never offer; they only answer. Because only one side of each pair ever offers, two peers can never send offers to each other at the same time ("glare"), and the protocol needs no rollback logic.

## Sequence: C joins a room that already holds A and B

```
 C                       Server                     A            B
 |--- join{room,name} --->|                          |            |
 |<-- joined{peers:[A,B]}-|--- peer-joined{C} ------>|            |
 |                        |--- peer-joined{C} ------------------->|
 |--- offer{to:A} ------->|--- offer{from:C} ------->|            |
 |--- offer{to:B} ------->|--- offer{from:C} ------------------->|
 |<-- answer{from:A} -----|<-- answer{to:C} ---------|            |
 |<-- answer{from:B} -----|<-- answer{to:C} ----------------------|
 |<== ice{...} both ways, trickled as candidates are gathered ==>|
 |                                                                |
 |========== SRTP media, peer-to-peer (UDP), not via server ======|
```

ICE candidates are sent as soon as they are gathered ("trickle ICE"). A candidate can arrive before the SDP it belongs to. The client queues it until `setRemoteDescription` has run.

## Leave / failure

```
 A                 Server                  B, C
 |--- leave ------->|--- peer-left{A} ----->|   (or socket close / missed heartbeat)
```

On `peer-left`, each remaining client closes its `RTCPeerConnection` to that peer and removes the tile.

## Stats side channel (HTTP, not part of signalling)

`POST /api/stats` with `{ run, rows[] }` appends rows to `results/<run>.csv`. `GET /api/config` returns the ICE (STUN) server list, so STUN can be changed on the server (`STUN_URLS` env) without editing the client. `GET /api/rooms` lists active rooms for debugging.
