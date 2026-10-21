// Builds Mini-Meet-Slides.pptx (run inside WSL/Linux):
//   mkdir -p ~/deckgen && cd ~/deckgen && npm install pptxgenjs     (once)
//   NODE_PATH=~/deckgen/node_modules node docs/slides/build_deck.js
// Numbers come from docs/slides/deck-data.json (python3 docs/slides/deck_data.py).
const path = require("path");
const fs = require("fs");
const pptxgen = require("pptxgenjs");

const ROOT = path.resolve(__dirname, "../..");
const OUT = path.join(ROOT, "Mini-Meet-Slides.pptx");
const SKILL = process.env.PPTX_SKILL; // folder holding scripts/apply_theme.js (optional)
const D = JSON.parse(fs.readFileSync(path.join(__dirname, "deck-data.json"), "utf8"));

const THEME = {
  name: "Mini-Meet",
  headFontFace: "Cambria",
  bodyFontFace: "Calibri",
  colors: {
    dk1: "14213D", lt1: "FFFFFF", dk2: "3D4A5C", lt2: "EEF1F5",
    accent1: "E0782B", accent2: "2F5BB5", accent3: "2E8B57", accent4: "B23A48",
    accent5: "7A8599", accent6: "F2B134", hlink: "2F5BB5", folHlink: "6B4FA0",
  },
};
const HEX = THEME.colors;
const OFF = "5B6779"; // chart: fixed rate / buffer off
const ON = "E0782B";  // chart: adaptive / buffer on

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.333 x 7.5 in
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
pres.title = "Mini-Meet: real-time video conferencing in C";
pres.subject = "Computer Networks course project 16 (also covers project 11)";
const C = pres.SchemeColor;
const S = pres.shapes;

// ---------- layouts ----------
pres.defineSlideMaster({
  title: "TITLE_DARK",
  background: { color: C.text1 },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.8, y: 2.2, w: 7.6, h: 1.4, fontSize: 60, bold: true, color: C.background1, valign: "bottom", align: "left", margin: 0 }, text: "" } },
    { placeholder: { options: { name: "body", type: "body", x: 0.8, y: 3.75, w: 7.2, h: 1.6, fontSize: 20, color: C.background2, valign: "top", margin: 0 }, text: "" } },
  ],
});
pres.defineSlideMaster({
  title: "CONTENT",
  background: { color: C.background1 },
  margin: [0.5, 0.6, 0.6, 0.6],
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 0.35, w: 12.1, h: 0.85, fontSize: 32, bold: true, color: C.text1, valign: "middle", align: "left", margin: 0 }, text: "" } },
    { text: { text: "Mini-Meet · Computer Networks", options: { x: 0.6, y: 7.0, w: 5, h: 0.3, fontSize: 10, color: HEX.accent5, margin: 0 } } },
  ],
  slideNumber: { x: 12.1, y: 7.0, w: 0.6, h: 0.3, fontSize: 10, color: HEX.accent5, align: "right" },
});
pres.defineSlideMaster({
  title: "CLOSING_DARK",
  background: { color: C.text1 },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.8, y: 0.55, w: 11.7, h: 0.9, fontSize: 36, bold: true, color: C.background1, valign: "middle", align: "left", margin: 0 }, text: "" } },
  ],
});

// ---------- helpers ----------
let n = 0;
const id = (s) => `${s}-${++n}`;
function text(slide, t, o) {
  slide.addText(t, Object.assign({ isTextBox: true, margin: 0, fontSize: 16, color: C.text2, valign: "top", objectName: id("text") }, o));
}
function card(slide, x, y, w, h, fill) {
  slide.addShape(S.ROUNDED_RECTANGLE, { x, y, w, h, rectRadius: 0.12, fill: { color: fill || C.background2 }, line: { type: "none" }, objectName: id("card") });
}
function badge(slide, x, y, label, fill) { // the deck's motif: a numbered/lettered circle
  slide.addShape(S.OVAL, { x, y, w: 0.48, h: 0.48, fill: { color: fill || C.accent1 }, line: { type: "none" }, objectName: id("badge") });
  slide.addText(label, { isTextBox: true, x, y, w: 0.48, h: 0.48, margin: 0, align: "center", valign: "middle", fontSize: 15, bold: true, color: C.background1, objectName: id("badge-text") });
}
function bullets(slide, items, o) {
  slide.addText(items.map((t, i) => ({ text: t, options: { bullet: { indent: 16 }, breakLine: i < items.length - 1 } })),
    Object.assign({ isTextBox: true, margin: 0, fontSize: 16, color: C.text2, valign: "top", paraSpaceAfter: 6, objectName: id("list") }, o));
}
function arrow(slide, x, y, w, h, color, o) {
  slide.addShape(S.LINE, Object.assign({ x, y, w, h, line: { color, width: 2.5, endArrowType: "triangle" }, objectName: id("arrow") }, o));
}
function stat(slide, x, y, w, big, label, color) {
  text(slide, big, { x, y, w, h: 0.9, fontSize: 40, bold: true, color: color || C.accent1, fontFace: THEME.headFontFace, valign: "bottom" });
  text(slide, label, { x, y: y + 0.95, w, h: 0.75, fontSize: 14, color: C.text2 });
}
const chartText = { catAxisLabelFontFace: "+mn-lt", valAxisLabelFontFace: "+mn-lt", legendFontFace: "+mn-lt", titleFontFace: "+mn-lt", dataLabelFontFace: "+mn-lt" };
function ffill(a) { let last = 0; return a.map((v) => (v === null || v === undefined || Number.isNaN(v) ? last : (last = v))); }
const P = D.profiles;

// ---------- 1. title ----------
pres.addSection({ title: "Overview" });
let s = pres.addSlide({ masterName: "TITLE_DARK", sectionTitle: "Overview" });
s.addText("Mini-Meet", { placeholder: "title" });
s.addText("Real-time video calls written in C: our own signalling, NAT traversal, RTP media engine, jitter buffer, rate control and end-to-end encryption", { placeholder: "body" });
text(s, "COMPUTER NETWORKS · PROJECT 16 · ALSO COVERS PROJECT 11", { x: 0.8, y: 1.55, w: 8, h: 0.4, fontSize: 14, bold: true, color: C.accent1, charSpacing: 1 });
text(s, "Windows + Linux · C11 · FFmpeg · UDP sockets · tc netem · github.com/TowVansh/mini-meet", { x: 0.8, y: 6.55, w: 9, h: 0.35, fontSize: 13, color: C.accent5 });
// motif: three call nodes (server + two peers)
s.addShape(S.LINE, { x: 9.05, y: 2.55, w: 1.25, h: 1.6, line: { color: HEX.accent2, width: 2, dashType: "dash" }, objectName: id("net"), flipH: true });
s.addShape(S.LINE, { x: 10.8, y: 2.55, w: 1.25, h: 1.6, line: { color: HEX.accent2, width: 2, dashType: "dash" }, objectName: id("net") });
s.addShape(S.LINE, { x: 8.8, y: 4.6, w: 3.5, h: 0, line: { color: HEX.accent1, width: 5 }, objectName: id("net") });
s.addShape(S.OVAL, { x: 10.05, y: 1.6, w: 1.0, h: 1.0, fill: { color: C.accent2 }, line: { type: "none" }, objectName: id("node") });
s.addShape(S.OVAL, { x: 8.25, y: 4.05, w: 1.1, h: 1.1, fill: { color: C.accent1 }, line: { type: "none" }, objectName: id("node") });
s.addShape(S.OVAL, { x: 11.75, y: 4.05, w: 1.1, h: 1.1, fill: { color: C.accent1 }, line: { type: "none" }, objectName: id("node") });
text(s, "server", { x: 9.8, y: 2.65, w: 1.5, h: 0.3, fontSize: 12, color: C.background2, align: "center" });
text(s, "peer-to-peer media", { x: 9.1, y: 4.7, w: 2.9, h: 0.3, fontSize: 12, color: C.background2, align: "center" });
s.addNotes("Mini-Meet is a 2 to 4 person video calling app we built in C from the socket level up. The server only introduces the peers; audio and video then go directly between them over UDP. It covers Project 16 and, as we will show, all deliverables of Project 11 as well.");

// ---------- 2. what we built ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Overview" });
s.addText("Every deliverable of both briefs is working", { placeholder: "title" });
text(s, "Project 16: video conferencing", { x: 0.6, y: 1.45, w: 5.7, h: 0.4, fontSize: 18, bold: true, color: C.text1 });
bullets(s, ["2–4 person audio + video call (VP8 + Opus)", "Custom signalling server and documented protocol", "STUN NAT traversal across separate networks", "Quality measured under loss, jitter, bandwidth caps", "Architecture: media path vs signalling path"], { x: 0.6, y: 1.9, w: 5.9, h: 2.1, fontSize: 15 });
text(s, "Project 11: adaptive streaming", { x: 0.6, y: 4.05, w: 5.7, h: 0.4, fontSize: 18, bold: true, color: C.text1 });
bullets(s, ["Live or recorded media over UDP", "Own framing layer: sequence numbers + timestamps", "Jitter buffer with measured playout smoothness", "Bitrate adaptation across changing conditions"], { x: 0.6, y: 4.5, w: 5.9, h: 1.7, fontSize: 15 });
text(s, "Beyond the briefs: end-to-end encryption · Windows + Linux · 72 unit-test checks", { x: 0.6, y: 6.35, w: 6.2, h: 0.35, fontSize: 14, italic: true, color: C.accent1 });
s.addImage({ path: path.join(ROOT, "docs/img/app-window.png"), x: 6.85, y: 1.6, w: 5.85, h: 3.44, objectName: id("screenshot") });
text(s, "Client window: own view (left), remote peer with live stats (path, safety code, RTT, loss, jitter, freezes)", { x: 6.85, y: 5.15, w: 5.85, h: 0.6, fontSize: 12, color: C.accent5 });
s.addNotes("Left: the deliverables from the two briefs. Right: the actual client window. The overlay shows the selected network path, the encryption safety code, RTT, loss, jitter and freezes, measured live.");

// ---------- 3. architecture ----------
pres.addSection({ title: "How it works" });
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Two paths: signalling via the server, media peer-to-peer", { placeholder: "title" });
s.addShape(S.ROUNDED_RECTANGLE, { x: 5.05, y: 1.45, w: 3.2, h: 0.95, rectRadius: 0.12, fill: { color: C.text1 }, line: { type: "none" }, objectName: id("server") });
text(s, [{ text: "mm-server", options: { bold: true, fontSize: 18, color: C.background1, breakLine: true } }, { text: "TCP 9000 · rooms · relay", options: { fontSize: 13, color: C.background2 } }], { x: 5.05, y: 1.45, w: 3.2, h: 0.95, align: "center", valign: "middle" });
for (const [x, name] of [[0.6, "mm (Alice)"], [9.55, "mm (Bob)"]]) {
  s.addShape(S.ROUNDED_RECTANGLE, { x, y: 3.1, w: 3.15, h: 1.15, rectRadius: 0.12, fill: { color: C.background1 }, line: { color: HEX.dk1, width: 1.5 }, objectName: id("peer") });
  text(s, [{ text: name, options: { bold: true, fontSize: 18, color: C.text1, breakLine: true } }, { text: "capture · encode · decode · play", options: { fontSize: 13, color: C.text2 } }], { x: x + 0.15, y: 3.1, w: 2.85, h: 1.15, align: "center", valign: "middle" });
}
s.addShape(S.LINE, { x: 2.2, y: 2.15, w: 2.85, h: 0.95, line: { color: HEX.accent2, width: 2, dashType: "dash", beginArrowType: "triangle", endArrowType: "triangle" }, flipV: true, objectName: id("tcp") });
s.addShape(S.LINE, { x: 8.25, y: 2.15, w: 2.85, h: 0.95, line: { color: HEX.accent2, width: 2, dashType: "dash", beginArrowType: "triangle", endArrowType: "triangle" }, objectName: id("tcp") });
s.addShape(S.LINE, { x: 3.85, y: 3.68, w: 5.6, h: 0, line: { color: HEX.accent1, width: 6, beginArrowType: "triangle", endArrowType: "triangle" }, objectName: id("udp") });
text(s, "RTP / RTCP / STUN over UDP · encrypted", { x: 4.1, y: 3.2, w: 5.1, h: 0.35, fontSize: 14, bold: true, color: C.accent1, align: "center" });
text(s, "JOIN · CAND · KEY (TCP)", { x: 1.35, y: 2.05, w: 2.6, h: 0.35, fontSize: 13, color: C.accent2 });
const facts = [
  ["Signalling", "TCP, newline-delimited text, a few hundred bytes per call. Every message must arrive in order."],
  ["Media", "UDP, one port for RTP + RTCP + STUN, 0.1–1.2 Mbit/s. Late data is useless, so we repair selectively."],
  ["Robust", "If the server dies, running calls continue. The server never sees (or can decrypt) media."],
];
facts.forEach(([h, b], i) => {
  const x = 0.6 + i * 4.1;
  card(s, x, 4.85, 3.85, 1.75);
  text(s, h, { x: x + 0.25, y: 5.0, w: 3.4, h: 0.4, fontSize: 17, bold: true, color: C.text1 });
  text(s, b, { x: x + 0.25, y: 5.42, w: 3.4, h: 1.1, fontSize: 14 });
});
s.addNotes("Signalling is small and must be reliable, so it uses TCP through our server. Media is large and time critical, so it goes directly between the peers over UDP. The server handles a few hundred bytes per call and cannot see the media.");

// ---------- 4. signalling ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Signalling: a plain-text protocol over TCP", { placeholder: "title" });
s.addShape(S.ROUNDED_RECTANGLE, { x: 0.6, y: 1.5, w: 5.9, h: 4.9, rectRadius: 0.12, fill: { color: C.text1 }, line: { type: "none" }, objectName: id("console") });
text(s, ["→ JOIN demo carol", "← JOINED c1 demo a1:alice", "→ CAND a1 srflx 49.36.1.2 50000", "→ KEY a1 3f9a…c41d", "← CAND a1 host 10.0.0.5 41000", "← KEY a1 77be…09aa", "→ PING", "← PONG"].join("\n"),
  { x: 0.95, y: 1.85, w: 5.3, h: 4.3, fontFace: "Consolas", fontSize: 17, color: C.background2, paraSpaceAfter: 8 });
const sig = [
  ["1", "Newline framing", "TCP is a byte stream; lines rebuild message boundaries."],
  ["2", "One select() loop", "Single thread, no locks, same code on Winsock and POSIX."],
  ["3", "Validation", "512-byte lines, name rules, relay only inside a room, max 4."],
  ["4", "Liveness", "PING every 5 s. Clients trust live media over a server timeout."],
];
sig.forEach(([b, h, d], i) => {
  const y = 1.55 + i * 1.2;
  badge(s, 7.0, y + 0.05, b);
  text(s, h, { x: 7.7, y, w: 5.0, h: 0.4, fontSize: 18, bold: true, color: C.text1 });
  text(s, d, { x: 7.7, y: y + 0.42, w: 5.0, h: 0.65, fontSize: 15 });
});
s.addNotes("Every message is one line of text, so we can even drive the server by hand with telnet. The newcomer learns who is in the room, then both sides exchange network candidates and public keys. There is no offer/answer, so no glare problem.");

// ---------- 5. NAT traversal ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("NAT traversal: STUN plus UDP hole punching", { placeholder: "title" });
const steps = [
  ["Gather", "host = LAN IP; srflx = public IP:port from the STUN reply (XOR-MAPPED-ADDRESS)"],
  ["Exchange", "candidates sent as CAND lines through the signalling server"],
  ["Punch", "both send STUN checks at once; each outgoing packet opens its own NAT"],
  ["Select", "first pair that answers carries the media; keepalive every 2.5 s"],
];
steps.forEach(([h, d], i) => {
  const x = 0.6 + i * 3.1;
  card(s, x, 1.55, 2.8, 2.75);
  badge(s, x + 0.25, 1.75, String(i + 1));
  text(s, h, { x: x + 0.25, y: 2.35, w: 2.35, h: 0.45, fontSize: 19, bold: true, color: C.text1 });
  text(s, d, { x: x + 0.25, y: 2.85, w: 2.35, h: 1.35, fontSize: 14 });
  if (i < 3) arrow(s, x + 2.83, 2.92, 0.24, 0, HEX.accent5);
});
card(s, 0.6, 4.6, 7.4, 2.0, "FFF1E6");
text(s, "Live test: college Wi-Fi ↔ laptop on a mobile hotspot", { x: 0.85, y: 4.75, w: 7.0, h: 0.4, fontSize: 17, bold: true, color: C.text1 });
text(s, "Both behind NAT, different ISPs. Connected srflx→srflx (public mapping to public mapping) in 0.1–0.75 s, three times, ~9 minutes of calls. Nothing relayed.", { x: 0.85, y: 5.2, w: 7.0, h: 1.3, fontSize: 15 });
card(s, 8.3, 4.6, 4.4, 2.0);
text(s, "When it fails", { x: 8.55, y: 4.75, w: 4.0, h: 0.4, fontSize: 17, bold: true, color: C.text1 });
text(s, "Symmetric NAT gives a new port per destination, so STUN's answer is wrong for the peer. Detected after 15 s; would need a TURN relay.", { x: 8.55, y: 5.2, w: 4.0, h: 1.3, fontSize: 14 });
s.addNotes("A NAT only lets packets in that match a mapping it created. STUN tells each side its public address. Both then send checks to each other at the same time, so each outgoing packet opens a hole in its own NAT for the other side's packets. Our real test across two networks connected directly.");

// ---------- 6. framing ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Our framing layer: RTP-style packets on one UDP port", { placeholder: "title" });
const hdr = [
  [[0.6, 0.9, "V P X CC"], [1.5, 0.8, "M · PT"], [2.3, 3.9, "sequence number (16 bits)"]],
  [[0.6, 5.6, "timestamp (32 bits): media time, shared by all packets of a frame"]],
  [[0.6, 5.6, "SSRC (32 bits): stream id"]],
  [[0.6, 1.4, "S K · 1 byte"], [2.0, 2.9, "VP8 / Opus payload ≤ 1160 B"], [4.9, 1.3, "16 B tag"]],
];
hdr.forEach((rowCells, r) => {
  rowCells.forEach(([x, w, t]) => {
    const fill = r === 3 ? (x > 4 ? "FDE3CF" : "E3E9F6") : "EEF1F5";
    s.addShape(S.RECTANGLE, { x, y: 1.6 + r * 0.72, w, h: 0.62, fill: { color: fill }, line: { color: "C9D1DC", width: 1 }, objectName: id("field") });
    text(s, t, { x: x + 0.08, y: 1.6 + r * 0.72, w: w - 0.16, h: 0.62, fontSize: 12, color: C.text1, align: "center", valign: "middle" });
  });
});
text(s, "Header stays readable (needed for demultiplexing and statistics) but is authenticated; payload is encrypted.", { x: 0.6, y: 4.55, w: 5.6, h: 0.65, fontSize: 13, color: C.accent5 });
s.addTable([
  [{ text: "PT", options: { bold: true, fill: { color: "E3E9F6" } } }, { text: "Payload", options: { bold: true, fill: { color: "E3E9F6" } } }, { text: "Rule", options: { bold: true, fill: { color: "E3E9F6" } } }],
  ["96", "VP8, 90 kHz", "S = first packet, M = last packet of a frame; split at 1160 B (under the MTU)"],
  ["97", "VP8 resend", "same bytes and sequence number; kept out of loss statistics"],
  ["111", "Opus, 48 kHz", "one 20 ms frame per packet; timestamp +960 each"],
], { x: 6.55, y: 1.6, w: 6.15, colW: [0.6, 1.45, 4.1], fontSize: 13, color: HEX.dk2, border: { type: "solid", pt: 1, color: "C9D1DC" }, valign: "middle", margin: 0.06, objectName: id("pt-table") });
card(s, 6.55, 4.55, 6.15, 1.95);
text(s, "One port, demultiplexed by the first byte", { x: 6.8, y: 4.7, w: 5.7, h: 0.4, fontSize: 16, bold: true, color: C.text1 });
bullets(s, ["0–3 + magic cookie → STUN", "128–191, second byte 200–206 → RTCP (SR, NACK, PLI)", "128–191 otherwise → RTP"], { x: 6.8, y: 5.15, w: 5.7, h: 1.3, fontSize: 14 });
s.addNotes("The sequence number gives order and reveals loss; the timestamp is media time, which is what lets the receiver restore the sender's timing. Video frames are split under the MTU so IP never fragments them. Retransmissions use a separate payload type so our loss statistics stay honest.");

// ---------- 7. jitter buffer ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Jitter buffer: play by timestamp, not by arrival", { placeholder: "title" });
card(s, 0.6, 1.5, 12.1, 1.35);
text(s, [
  { text: "offset = arrival − media time     →     play media time T at  T + p95(last 64 offsets)", options: { fontSize: 18, bold: true, color: C.text1, breakLine: true } },
  { text: "About 95 % of packets are there in time; playout follows the sender's pace. The delay grows with jitter and shrinks when the network calms down.", options: { fontSize: 14, color: C.text2 } },
], { x: 0.9, y: 1.62, w: 11.6, h: 1.15, valign: "middle" });
stat(s, 0.6, 3.15, 3.8, "88 % → 0.1 %", "audio frames concealed at ±30 ms jitter (buffer off → on)");
stat(s, 4.75, 3.15, 3.8, "25 → 12 ms", "spread of video frame intervals (playout smoothness)");
stat(s, 8.9, 3.15, 3.8, "+140 ms", "the price: extra delay the buffer adds at ±30 ms (≈ 27 ms on a clean link)", HEX.accent2);
s.addChart(pres.charts.BAR, [
  { name: "buffer off", labels: ["±30 ms jitter", "±60 ms jitter"], values: [87.9, 83.6] },
  { name: "buffer on", labels: ["±30 ms jitter", "±60 ms jitter"], values: [P.jitter30.off.audio_conceal_pct, P.jitter60.off.audio_conceal_pct] },
], Object.assign({
  x: 0.6, y: 5.0, w: 12.1, h: 1.85, barDir: "bar", barGrouping: "clustered", chartColors: [OFF, ON],
  showValue: true, dataLabelPosition: "outEnd", dataLabelFormatCode: "0.0\"%\"", dataLabelFontSize: 11, dataLabelColor: HEX.dk2,
  catAxisLabelColor: HEX.dk2, catAxisLabelFontSize: 12, valAxisHidden: true, valAxisMaxVal: 100,
  valGridLine: { style: "none" }, catGridLine: { style: "none" }, showLegend: true, legendPos: "r", legendFontSize: 12, legendColor: HEX.dk2,
  showTitle: true, title: "Audio concealed (%)", titleFontSize: 12, titleColor: HEX.dk2, objectName: id("jb-chart"),
}, chartText));
s.addNotes("Packets leave every 20 milliseconds but arrive unevenly. We record how late each packet is relative to its timestamp and schedule playout at the 95th percentile. Without the buffer almost 90 percent of audio arrives too late at 30 ms jitter; with it, 0.1 percent. The cost is about 140 ms of extra delay: the classic smoothness versus latency trade-off.");

// ---------- 8. loss repair ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Loss repair: resend it, rebuild it, or hide it", { placeholder: "title" });
const rep = [
  ["NACK", "Sequence gap → ask for exactly the missing packets (ID + 16-bit mask). Resent every 1.5 × RTT, max 4 tries: selective repeat with a deadline."],
  ["Resend", "Sender keeps the last 1024 video packets and resends them as PT 97, so loss statistics show the network, not the repair."],
  ["Keyframe", "Not repaired in time → skip the frame; later frames depend on it, so request a keyframe (PLI). Seen as a freeze."],
  ["FEC / PLC", "Lost audio is rebuilt from the copy inside the next packet (Opus FEC), otherwise extrapolated (packet loss concealment)."],
];
rep.forEach(([h, d], i) => {
  const x = 0.6 + i * 3.1;
  card(s, x, 1.55, 2.85, 3.7);
  badge(s, x + 0.25, 1.75, String(i + 1));
  text(s, h, { x: x + 0.25, y: 2.4, w: 2.4, h: 0.45, fontSize: 19, bold: true, color: C.text1 });
  text(s, d, { x: x + 0.25, y: 2.9, w: 2.4, h: 2.25, fontSize: 14 });
});
stat(s, 0.6, 5.35, 6.0, "0 freezes", "with up to 5 % random loss (≤ 0.4 % of audio concealed)");
stat(s, 6.75, 5.35, 6.0, "23 → 15", "freezes at 20 % loss, rate control off → on (smaller frames, fewer packets to lose)", HEX.accent2);
s.addNotes("Loss is detected from sequence gaps. Video is repaired by retransmission if there is still time, otherwise the frame is skipped and a keyframe requested. Audio uses forward error correction: each packet carries a low-rate copy of the previous one.");

// ---------- 9. rate control ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("Rate control: AIMD driven by receiver reports", { placeholder: "title" });
s.addChart(pres.charts.BAR, [{ name: "video kbps", labels: ["0 · 640×480", "1 · 640×480", "2 · 480×360", "3 · 320×240 (start)", "4 · 320×240", "5 · 160×120"], values: [1200, 800, 500, 300, 180, 100] }],
  Object.assign({
    x: 0.6, y: 1.5, w: 6.0, h: 5.1, barDir: "col", chartColors: [HEX.dk1], showValue: true, dataLabelPosition: "outEnd", dataLabelFontSize: 12, dataLabelColor: HEX.dk2,
    catAxisLabelColor: HEX.dk2, catAxisLabelFontSize: 11, valAxisHidden: true, valGridLine: { style: "none" }, catGridLine: { style: "none" }, showLegend: false,
    showTitle: true, title: "Quality ladder: video kbps by level", titleFontSize: 13, titleColor: HEX.dk2, objectName: id("ladder"),
  }, chartText));
const rules = [
  ["−2", "Congestion: loss > 8 % or queuing delay > 120 ms → drop two levels (multiplicative decrease)"],
  ["+1", "Clean: loss < 2 % and queuing delay < 40 ms, three reports in a row → climb one level (additive increase)"],
  ["Δ", "Queuing delay = smoothed RTT − its 10 s minimum, capped 250 ms above the best ever (delay-based, like BBR / GCC)"],
  ["S", "Slow start at 300 kbps; feedback starvation: no report for 2.5 s = stuck in the queue = back off"],
];
rules.forEach(([b, d], i) => {
  const y = 1.6 + i * 1.25;
  badge(s, 6.95, y, b, i < 2 ? C.accent1 : C.accent2);
  text(s, d, { x: 7.6, y: y - 0.02, w: 5.1, h: 1.1, fontSize: 15 });
});
s.addNotes("Every second each receiver reports loss, jitter and RTT in RTCP. Our controller cuts quality fast and raises it slowly, like TCP Reno. It also watches the RTT rising above its baseline, which shows a queue filling before any packet is lost.");

// ---------- 10. security ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "How it works" });
s.addText("End-to-end encryption: the server can never see the media", { placeholder: "title" });
const sec = [
  ["Key agreement", "Fresh X25519 key pair per run; public keys swapped via the server (KEY line); both sides derive the same 2 session keys"],
  ["Every packet", "ChaCha20-Poly1305 AEAD; header readable but authenticated; nonce = SSRC · seq · PT · timestamp"],
  ["Tampering", "One flipped bit (even in the sequence number) fails the tag; packet dropped (AUTH-FAIL)"],
];
sec.forEach(([h, d], i) => {
  const x = 0.6 + i * 4.1;
  card(s, x, 1.55, 3.85, 2.7);
  badge(s, x + 0.25, 1.75, String(i + 1), C.accent2);
  text(s, h, { x: x + 0.25, y: 2.35, w: 3.4, h: 0.45, fontSize: 19, bold: true, color: C.text1 });
  text(s, d, { x: x + 0.25, y: 2.85, w: 3.4, h: 1.35, fontSize: 14 });
  if (i < 2) arrow(s, x + 3.88, 2.9, 0.19, 0, HEX.accent5);
});
card(s, 0.6, 4.55, 6.0, 2.05, "FFF1E6");
text(s, "Man-in-the-middle check", { x: 0.85, y: 4.7, w: 5.5, h: 0.4, fontSize: 17, bold: true, color: C.text1 });
text(s, "A malicious server could swap keys, so both screens show a safety code (hash of both public keys), e.g. 0D66-C339. Same code = no one in between.", { x: 0.85, y: 5.15, w: 5.5, h: 1.35, fontSize: 15 });
card(s, 6.85, 4.55, 5.85, 2.05);
text(s, "Server hardening", { x: 7.1, y: 4.7, w: 5.4, h: 0.4, fontSize: 17, bold: true, color: C.text1 });
text(s, "512-byte line limit (DoS), strict name/candidate/key checks, relay only within a room, 4-person cap, idle timeout. Not covered: TLS on signalling, media replay window.", { x: 7.1, y: 5.15, w: 5.4, h: 1.35, fontSize: 15 });
s.addNotes("Diffie-Hellman means only public keys cross the server, so it cannot derive the media keys. Each packet is encrypted and authenticated. To catch a server that swaps keys, users compare the safety code on both screens, like Signal's safety numbers.");

// ---------- 11. evaluation setup ----------
pres.addSection({ title: "Evaluation" });
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Evaluation" });
s.addText("Evaluation: 28 runs through emulated bad networks", { placeholder: "title" });
for (const [x, ns, who] of [[0.6, "namespace mmA · 10.10.0.1", "mm-server + client A"], [4.45, "namespace mmB · 10.10.0.2", "client B"]]) {
  card(s, x, 1.6, 3.2, 1.6);
  text(s, ns, { x: x + 0.2, y: 1.75, w: 2.8, h: 0.4, fontSize: 15, bold: true, color: C.text1 });
  text(s, who, { x: x + 0.2, y: 2.2, w: 2.8, h: 0.4, fontSize: 14 });
}
s.addShape(S.LINE, { x: 3.8, y: 2.4, w: 0.65, h: 0, line: { color: HEX.accent1, width: 4, beginArrowType: "triangle", endArrowType: "triangle" }, objectName: id("veth") });
text(s, "veth pair, tc netem on both ends: each direction impaired on its own, like a real access link", { x: 0.6, y: 3.35, w: 7.0, h: 0.7, fontSize: 14, color: C.accent5 });
bullets(s, ["10 s clean → profile applied mid-call → 45 s recorded", "Rate control off (fixed 1200 kbps) vs on, for every profile", "Per-second CSV: bitrate, fps, loss, jitter, RTT, freezes, concealment, playout spread, buffer delay", "Extra runs: jitter buffer on/off, changing bandwidth in one call, 4-person mesh, real cross-network call"], { x: 0.6, y: 4.2, w: 7.0, h: 2.5, fontSize: 15 });
s.addTable([
  [{ text: "Profile", options: { bold: true, fill: { color: "E3E9F6" } } }, { text: "netem (each direction)", options: { bold: true, fill: { color: "E3E9F6" } } }],
  ["loss 1 / 5 / 10 / 20", "random loss N %"],
  ["burst5", "bursty (Gilbert-Elliott) loss"],
  ["delay100", "100 ms fixed delay"],
  ["jitter 30 / 60", "100 ms ± 30 / 60 ms (reorders)"],
  ["rate 2m / 1m / 500k / 250k", "bandwidth cap, 1000-packet FIFO"],
  ["combo", "80 ± 20 ms, 3 % loss, 1 Mbit/s"],
], { x: 7.95, y: 1.6, w: 4.75, colW: [2.05, 2.7], fontSize: 13, color: HEX.dk2, border: { type: "solid", pt: 1, color: "C9D1DC" }, valign: "middle", margin: 0.06, objectName: id("profiles") });
s.addNotes("Two Linux network namespaces act as two hosts joined by a virtual cable. netem on both ends impairs each direction separately. Every profile is run with our rate controller off and on, and every client writes per-second statistics.");

// ---------- 12. bandwidth / bufferbloat ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Evaluation" });
s.addText("Bandwidth caps: rate control stops bufferbloat", { placeholder: "title" });
const r1 = P.rate1m, r5 = P.rate500k, cb = P.combo;
stat(s, 0.6, 1.5, 4.1, `${(r1.off.rtt_ms / 1000).toFixed(1)} s → ${Math.round(r1.on.rtt_ms)} ms`, "RTT on a 1 Mbit/s link, fixed rate → adaptive");
stat(s, 0.6, 3.25, 4.1, `${Math.round(r5.off.video_fps)} → ${Math.round(r5.on.video_fps)} fps`, "video on a 500 kbit/s link (fixed rate freezes completely)");
stat(s, 0.6, 5.0, 4.1, `${Math.round(cb.off.video_fps)} → ${Math.round(cb.on.video_fps)} fps`, "combined delay + loss + 1 Mbit/s: fixed call stalls, adaptive keeps running", HEX.accent2);
const prof = ["loss5", "loss20", "jitter60", "rate1m", "rate500k", "rate250k", "combo"];
const lab = ["loss 5 %", "loss 20 %", "jitter ±60", "1 Mbit/s", "500 kbit/s", "250 kbit/s", "combo"];
s.addChart(pres.charts.BAR, [
  { name: "rate control off", labels: lab, values: prof.map((p) => P[p].off.audio_conceal_pct) },
  { name: "rate control on", labels: lab, values: prof.map((p) => P[p].on.audio_conceal_pct) },
], Object.assign({
  x: 5.0, y: 1.5, w: 7.7, h: 5.2, barDir: "col", barGrouping: "clustered", chartColors: [OFF, ON],
  showValue: true, dataLabelPosition: "outEnd", dataLabelFormatCode: "0", dataLabelFontSize: 10, dataLabelColor: HEX.dk2,
  catAxisLabelColor: HEX.dk2, catAxisLabelFontSize: 11, valAxisLabelColor: HEX.dk2, valAxisLabelFontSize: 10, valAxisMaxVal: 100,
  valGridLine: { color: "E1E5EB", size: 0.5 }, catGridLine: { style: "none" }, showLegend: true, legendPos: "t", legendFontSize: 12, legendColor: HEX.dk2,
  showTitle: true, title: "Audio frames concealed (%) by impairment", titleFontSize: 13, titleColor: HEX.dk2, objectName: id("conceal-chart"),
}, chartText));
s.addNotes("Without rate control, sending 1.2 Mbit/s into a 1 Mbit/s link fills the network queue: RTT climbs past 10 seconds and then packets are dropped. That is bufferbloat. Our controller backs off within seconds and keeps RTT in milliseconds. Only bandwidth caps without rate control ruin the audio; loss and jitter are handled by FEC and the jitter buffer.");

// ---------- 13. changing conditions ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Evaluation" });
s.addText("One call through changing bandwidth: the sender follows", { placeholder: "title" });
const SCH = D.schedule;
const phaseName = { baseline: "clean", rate2m: "2 Mbit/s", rate1m: "1 Mbit/s", rate500k: "500 kbit/s" };
const marks = {};
SCH.phases.forEach((p) => { marks[Math.max(0, p.t)] = phaseName[p.name] || p.name; });
const cat = SCH.t.map((t) => marks[t] || "");
s.addChart(pres.charts.LINE, [
  { name: "fixed rate", labels: cat, values: ffill(SCH.kbps_off) },
  { name: "adaptive", labels: cat, values: ffill(SCH.kbps_on) },
], Object.assign({
  x: 0.6, y: 1.4, w: 12.1, h: 2.85, chartColors: [OFF, ON], lineSize: 2, lineDataSymbol: "none",
  catAxisLabelColor: HEX.dk2, catAxisLabelFontSize: 11, catAxisLabelFrequency: 1, catAxisLabelRotate: 0,
  valAxisLabelColor: HEX.dk2, valAxisLabelFontSize: 10, valAxisMaxVal: 1400, valGridLine: { color: "E1E5EB", size: 0.5 }, catGridLine: { style: "none" },
  showLegend: true, legendPos: "r", legendFontSize: 12, legendColor: HEX.dk2,
  showTitle: true, title: "Sending rate (kbps): network goes clean → 2 → 1 → 0.5 → 1 Mbit/s → clean", titleFontSize: 12, titleColor: HEX.dk2, objectName: id("sched-kbps"),
}, chartText));
s.addChart(pres.charts.LINE, [
  { name: "fixed rate", labels: cat, values: ffill(SCH.rtt_off_s) },
  { name: "adaptive", labels: cat, values: ffill(SCH.rtt_on_s) },
], Object.assign({
  x: 0.6, y: 4.35, w: 12.1, h: 2.4, chartColors: [OFF, ON], lineSize: 2, lineDataSymbol: "none",
  catAxisLabelColor: HEX.dk2, catAxisLabelFontSize: 11, catAxisLabelFrequency: 1, catAxisLabelRotate: 0,
  valAxisLabelColor: HEX.dk2, valAxisLabelFontSize: 10, valGridLine: { color: "E1E5EB", size: 0.5 }, catGridLine: { style: "none" },
  showLegend: true, legendPos: "r", legendFontSize: 12, legendColor: HEX.dk2,
  showTitle: true, title: "Round-trip time (s): fixed rate reaches 14 s, adaptive stays near zero", titleFontSize: 12, titleColor: HEX.dk2, objectName: id("sched-rtt"),
}, chartText));
s.addNotes("This is a single call while we change the network every 20 seconds. The adaptive sender cuts within seconds of every drop, probes back up in a sawtooth and returns to full quality about 17 seconds after the network clears. The fixed-rate sender builds a 14 second delay and its video stops until the network is clean again.");

// ---------- 14. real-world checks ----------
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Evaluation" });
s.addText("Real-world checks beyond the emulator", { placeholder: "title" });
const real = [
  ["12 / 12", "streams at 640×480, 20 fps in a 4-person mesh (each client uploads 3 copies, ~3.6 Mbit/s)"],
  ["srflx→srflx", "college Wi-Fi ↔ mobile hotspot, median RTT 85 ms; hotspot spikes to 1.15 s were followed by the controller"],
  ["20 fps", "recorded file (H.264 + AAC) streamed in real time, 0 % audio concealed"],
  ["72 / 72", "unit-test checks pass on Windows and Linux: STUN, RTP/RTCP, stats maths, jitter buffer, AIMD, crypto, tamper"],
];
real.forEach(([big, d], i) => {
  const x = 0.6 + (i % 2) * 6.15, y = 1.55 + Math.floor(i / 2) * 2.6;
  card(s, x, y, 5.95, 2.35);
  text(s, big, { x: x + 0.3, y: y + 0.2, w: 5.4, h: 0.85, fontSize: 38, bold: true, color: i % 2 ? C.accent2 : C.accent1, fontFace: THEME.headFontFace, valign: "middle" });
  text(s, d, { x: x + 0.3, y: y + 1.1, w: 5.4, h: 1.1, fontSize: 15 });
});
s.addNotes("Beyond the emulator: four people in one room, a real call between two different networks, streaming a recorded file, and 72 unit-test checks on both operating systems.");

// ---------- 15. limitations ----------
pres.addSection({ title: "Wrap-up" });
s = pres.addSlide({ masterName: "CONTENT", sectionTitle: "Wrap-up" });
s.addText("Known limits, and what we would do next", { placeholder: "title" });
const lim = [
  ["Random loss and jitter look like congestion", "classify loss by delay trend before backing off"],
  ["250 kbit/s cap: queue already full before feedback returns", "emergency pause-video level; AQM (CoDel) in the network"],
  ["Jitter buffer adds up to ~220 ms under heavy jitter", "trade latency vs smoothness per application (voice vs video)"],
  ["No TURN relay: two symmetric NATs cannot connect", "add a TURN fallback"],
  ["Mesh tops out near 4 people", "SFU with simulcast so each receiver gets what its link supports"],
];
text(s, "Limit", { x: 0.6, y: 1.45, w: 6.0, h: 0.4, fontSize: 16, bold: true, color: C.accent5 });
text(s, "Next step", { x: 7.0, y: 1.45, w: 5.7, h: 0.4, fontSize: 16, bold: true, color: C.accent5 });
lim.forEach(([l, nx], i) => {
  const y = 1.95 + i * 0.95;
  card(s, 0.6, y, 6.1, 0.8);
  text(s, l, { x: 0.85, y, w: 5.7, h: 0.8, fontSize: 15, color: C.text1, valign: "middle" });
  arrow(s, 6.75, y + 0.4, 0.2, 0, HEX.accent1);
  card(s, 7.0, y, 5.7, 0.8, "FFF1E6");
  text(s, nx, { x: 7.25, y, w: 5.3, h: 0.8, fontSize: 15, color: C.text1, valign: "middle" });
});
s.addNotes("These weaknesses are the textbook ones for this kind of design; each has a known fix.");

// ---------- 16. takeaways ----------
s = pres.addSlide({ masterName: "CLOSING_DARK", sectionTitle: "Wrap-up" });
s.addText("Takeaways", { placeholder: "title" });
const take = [
  ["Both halves of the loop matter", "jitter buffer at the receiver (88 % → 0.1 % concealed), rate control at the sender (10.5 s → 3 ms RTT)"],
  ["Everything is our own C", "sockets, signalling, STUN, framing, repair, rate control and encryption, on Windows and Linux"],
  ["Measured, not assumed", "28 impairment runs, jitter-buffer A/B, changing bandwidth, mesh and a real cross-network call"],
];
take.forEach(([h, d], i) => {
  const y = 1.75 + i * 1.3;
  badge(s, 0.8, y + 0.05, String(i + 1));
  text(s, h, { x: 1.55, y, w: 11.0, h: 0.45, fontSize: 22, bold: true, color: C.background1 });
  text(s, d, { x: 1.55, y: y + 0.5, w: 11.0, h: 0.6, fontSize: 16, color: C.background2 });
});
s.addShape(S.ROUNDED_RECTANGLE, { x: 0.8, y: 5.65, w: 11.7, h: 1.2, rectRadius: 0.12, fill: { color: "1F2E52" }, line: { type: "none" }, objectName: id("demo") });
text(s, [{ text: "Live demo", options: { bold: true, fontSize: 15, color: C.accent1, breakLine: true } }, { text: "mm-server   ·   mm --server 127.0.0.1 --room demo --name A   ·   mm ... --name B --test", options: { fontFace: "Consolas", fontSize: 14, color: C.background2 } }], { x: 1.1, y: 5.75, w: 11.2, h: 1.0, valign: "middle" });
s.addNotes("To summarise: a real-time stream needs smoothing at the receiver and rate control at the sender, driven by receiver feedback. We built every part ourselves and measured it. Now a live demo.");

// ---------- write ----------
(async () => {
  await pres.writeFile({ fileName: OUT });
  if (SKILL) {
    const { applyTheme } = require(path.join(SKILL, "scripts/apply_theme.js"));
    await applyTheme(OUT, THEME);
  }
  console.log("wrote", OUT);
})();
