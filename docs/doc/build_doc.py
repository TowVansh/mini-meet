"""Build docs/documentation.html: one section per expected outcome, with real code and output.

    python3 docs/doc/build_doc.py    (WSL; needs Pillow, then print the HTML to PDF with Chrome)

Code is cut from the source files and terminal images are drawn from the captured
output in docs/doc/out (see capture.sh), so neither can drift from the project.
"""

import html
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).parent / "out"
IMG = ROOT / "docs" / "img"
FONT = ImageFont.truetype("/mnt/c/Windows/Fonts/consola.ttf", 26)

REPO = "https://github.com/TowVansh/mini-meet"


def terminal(name):
    """Draw a captured output file as a terminal-style image; returns its relative path."""
    lines = (OUT / f"{name}.txt").read_text().rstrip().split("\n")
    w = max(int(FONT.getlength(l)) for l in lines) + 48
    im = Image.new("RGB", (w, 34 * len(lines) + 40), "#101418")
    d = ImageDraw.Draw(im)
    for i, l in enumerate(lines):
        d.text((24, 20 + i * 34), l, font=FONT, fill="#8fe388" if l.startswith("$") else "#e6e8eb")
    im.save(IMG / f"term-{name}.png")
    return f"img/term-{name}.png"


def func(path, name):
    """Source of one C function, from its signature to the closing brace in column 0."""
    src = (ROOT / path).read_text().split("\n")
    start = next(i for i, l in enumerate(src) if re.match(rf"^[A-Za-z].*\b{name}\s*\(", l) and not l.rstrip().endswith(";"))
    end = next(i for i in range(start, len(src)) if src[i].startswith("}"))
    return "\n".join(src[start:end + 1])


def code(path, *names):
    body = "\n\n".join(func(path, n) for n in names)
    return f'<div class="file">{path}</div><pre>{html.escape(body)}</pre>'


def shell_block(path, start, end):
    src = (ROOT / path).read_text()
    return f'<div class="file">{path}</div><pre>{html.escape(src[src.index(start):src.index(end)].rstrip())}</pre>'


def fig(src, caption, width=150):
    return f'<figure><img src="{src}" style="max-width:100%;width:{width}mm"><figcaption>{caption}</figcaption></figure>'


OUTCOMES = [
    ("Working multi-party call with audio and video", "Done"),
    ("Custom signalling server and documented signalling protocol", "Done"),
    ("NAT traversal demonstration across separate networks (STUN)", "Done"),
    ("Call-quality measurements under packet loss, jitter and bandwidth limits", "Done"),
    ("Architecture document covering media path vs. signalling path", "Done"),
]

report = (ROOT / "docs" / "report.html").read_text()
diagram = re.search(r"<svg.*?</svg>", report, re.S)[0]

front = f"""
<section class="front">
<div class="course">Computer Networks · Project Documentation</div>
<h1 class="title">Video Conferencing Application (Mini-Meet)</h1>
<table class="info">
<tr><th>Project number</th><td>16</td></tr>
<tr><th>Project title</th><td>Video Conferencing Application (Mini-Meet)<br>Group B: Real-World Application Projects</td></tr>
<tr><th>Team members</th><td>Suvansh Choudhary (25BAI0252)<br>Priyam Sharma (25BAI0159)<br>Avika Agarwal (25BBS0057)</td></tr>
<tr><th>Source code</th><td><a href="{REPO}">{REPO}</a></td></tr>
</table>
<h2>Expected outcomes and status</h2>
<table>
<tr><th style="width:8%">No.</th><th>Expected outcome</th><th style="width:14%">Status</th></tr>
{"".join(f'<tr><td>{i}</td><td>{o}</td><td class="done">{s}</td></tr>' for i, (o, s) in enumerate(OUTCOMES, 1))}
</table>
</section>"""

s1 = f"""
<h1>Outcome 1: Working multi-party call with audio and video <span class="done">Done</span></h1>
<p>Each client captures from the webcam and microphone, encodes VP8 video and Opus audio, and sends RTP packets over UDP directly to every other participant (full mesh, up to 4 people). Each video frame is split into packets that fit the MTU, and each packet is copied to every connected peer.</p>
<h2>Code</h2>
{code("src/client/media_in.c", "fanout", "send_video_frame")}
<h2>Implementation</h2>
{fig("img/output-4party.png", "Four clients in one call, seen from PC1, using the built-in test video and tone. Each remote tile shows its live statistics.")}
<p>All 12 directed video streams of a 4-person call arrived at 640×480 and 20 fps, about 1190–1220 kbps each. The webcam and microphone call is shown live in the demo.</p>
"""

s2 = f"""
<h1>Outcome 2: Custom signalling server and documented signalling protocol <span class="done">Done</span></h1>
<p><code>mm-server</code> is a single-threaded <code>select()</code> server. It groups clients into rooms of up to 4 and relays network candidates and public keys inside a room. Every message is one line of text over TCP. The full protocol is documented in <code>docs/PROTOCOL.md</code>.</p>
<table>
<tr><th>Client to server</th><th>Server to client</th></tr>
<tr><td><code>JOIN &lt;room&gt; &lt;name&gt;</code></td><td><code>JOINED &lt;id&gt; &lt;room&gt; [id:name ...]</code></td></tr>
<tr><td><code>CAND &lt;to&gt; &lt;type&gt; &lt;ip&gt; &lt;port&gt;</code></td><td><code>PEER_JOINED &lt;id&gt; &lt;name&gt;</code>, <code>PEER_LEFT &lt;id&gt;</code></td></tr>
<tr><td><code>KEY &lt;to&gt; &lt;64 hex&gt;</code></td><td><code>CAND &lt;from&gt; ...</code>, <code>KEY &lt;from&gt; ...</code></td></tr>
<tr><td><code>LEAVE</code>, <code>PING</code></td><td><code>PONG</code>, <code>ERROR &lt;code&gt; &lt;text&gt;</code></td></tr>
</table>
<h2>Code</h2>
{code("src/server/mm_server.c", "handle_join", "handle_cand")}
{code("src/common/proto.c", "mm_linebuf_pop")}
<h2>Implementation</h2>
{fig(terminal("signal"), "Protocol exchange with the running server: two clients join, ping, a rejected second JOIN, and a leave.", 125)}
{fig(terminal("server"), "Server log for the same session.", 125)}
"""

s3 = f"""
<h1>Outcome 3: NAT traversal demonstration across separate networks (STUN) <span class="done">Done</span></h1>
<p>Each client asks a STUN server for its public address (the server-reflexive, or srflx, candidate) and sends its candidates through signalling. Both peers then send STUN checks to each other at the same time, which opens a path through both NATs (UDP hole punching). The first candidate that answers carries the media.</p>
<h2>Code</h2>
{code("src/stun/stun.c", "stun_build_success")}
{code("src/client/ice.c", "send_check", "ice_tick")}
<h2>Implementation</h2>
{fig(terminal("nat"), "Client log from the live test on 2026-10-08: this laptop on college Wi-Fi, the other (Friend) on a phone hotspot. Different private and public addresses; connected directly, srflx to srflx.", 160)}
<p>All 1404 recorded samples of these calls (<code>results/nat_c__Laptop.csv</code>) show <code>srflx-&gt;srflx</code>, with a median round-trip time of 85 ms. Signalling went through a TCP tunnel; media went directly between the laptops. Not covered: two symmetric NATs would need a TURN relay, which is not implemented. The client detects this case and reports it after 15 s.</p>
"""

s4 = f"""
<h1>Outcome 4: Call-quality measurements under packet loss, jitter and bandwidth limits <span class="done">Done</span></h1>
<p>Two Linux network namespaces joined by a virtual link act as two hosts, with <code>tc netem</code> impairing each direction. Each of 14 profiles ran with rate control off and on (28 runs). Every client records bitrate, fps, loss, jitter, round-trip time, freezes and audio concealment each second. The full analysis is in <code>docs/EVALUATION.md</code>.</p>
<table>
<tr><th>Condition</th><th>Rate control</th><th class="n">fps</th><th class="n">Loss %</th><th class="n">Round-trip ms</th><th class="n">Freezes</th><th class="n">Audio concealed %</th></tr>
<tr><td>No impairment</td><td>on</td><td class="n">20</td><td class="n">0.0</td><td class="n">0.7</td><td class="n">0</td><td class="n">0.0</td></tr>
<tr><td>Packet loss 5 %</td><td>off</td><td class="n">20</td><td class="n">4.9</td><td class="n">0.3</td><td class="n">0</td><td class="n">0.2</td></tr>
<tr><td>Packet loss 20 %</td><td>off</td><td class="n">19</td><td class="n">19.8</td><td class="n">0.2</td><td class="n">23</td><td class="n">4.0</td></tr>
<tr><td>Jitter ±60 ms</td><td>off</td><td class="n">20</td><td class="n">1.7</td><td class="n">194</td><td class="n">4</td><td class="n">0.2</td></tr>
<tr><td>Bandwidth 1 Mbit/s</td><td>off</td><td class="n">15</td><td class="n">13.6</td><td class="n">10513</td><td class="n">0</td><td class="n">34.1</td></tr>
<tr><td>Bandwidth 1 Mbit/s</td><td>on</td><td class="n">20</td><td class="n">0.0</td><td class="n">2.8</td><td class="n">0</td><td class="n">2.7</td></tr>
<tr><td>Bandwidth 500 kbit/s</td><td>off</td><td class="n">0</td><td class="n">39.6</td><td class="n">19414</td><td class="n">2</td><td class="n">49.0</td></tr>
<tr><td>Bandwidth 500 kbit/s</td><td>on</td><td class="n">20</td><td class="n">0.0</td><td class="n">9.8</td><td class="n">0</td><td class="n">11.7</td></tr>
</table>
<h2>Code</h2>
{shell_block("scripts/impair.sh", "profile_args() {", "PROFILES=")}
{code("src/client/rtp.c", "rx_stats_update")}
{code("src/client/cc.c", "cc_on_report")}
<h2>Implementation</h2>
{fig(terminal("netem"), "Impairment profiles applied with tc netem.", 125)}
{fig(terminal("cc"), "Rate controller log during the 1 Mbit/s run: it backs off when the round-trip time rises, then climbs again.", 150)}
{fig("../results/timeline_rate1m.png", "1 Mbit/s cap applied at 9 s. Fixed rate (blue): the round-trip time climbs past 10 s and loss reaches 13.6 %. With rate control: the call stays at 20 fps.", 125)}
{fig("../results/timeline_schedule.png", "One call through changing bandwidth: clean, 2 Mbit/s, 1 Mbit/s, 500 kbit/s, 1 Mbit/s, clean.", 125)}
{fig("img/output-dashboard.png", "Dashboard (analysis/dashboard.py): live statistics of a running call, above the evaluation results.", 125)}
"""

s5 = f"""
<h1>Outcome 5: Architecture document covering media path vs. signalling path <span class="done">Done</span></h1>
<p>The full architecture document is <code>docs/ARCHITECTURE.md</code> in the repository. In short, signalling goes through the server over TCP, and media goes directly between peers over UDP.</p>
<figure>{diagram}<figcaption>Signalling path (dashed, TCP) and media path (solid, UDP).</figcaption></figure>
<table>
<tr><th></th><th>Signalling path</th><th>Media path</th></tr>
<tr><td>Purpose</td><td>Find peers, exchange network candidates and public keys</td><td>Carry audio and video</td></tr>
<tr><td>Endpoints</td><td>Client to <code>mm-server</code></td><td>Client to client (full mesh)</td></tr>
<tr><td>Transport</td><td>TCP, one text line per message</td><td>UDP: RTP, RTCP and STUN on one port</td></tr>
<tr><td>Volume</td><td>A few hundred bytes per call setup</td><td>0.1–1.2 Mbit/s per stream</td></tr>
<tr><td>If the server stops</td><td>New joins fail</td><td>Running calls continue</td></tr>
</table>
<h2>Code</h2>
<p>Where the two paths meet in the client: one <code>select()</code> loop watches the TCP signalling socket and the UDP media socket, and each UDP packet is sorted into STUN, RTCP or RTP by its first bytes.</p>
{code("src/client/netloop.c", "on_udp")}
<h2>Implementation</h2>
{fig(terminal("test"), "Unit tests covering the protocol code on both paths.", 70)}
"""

PAGE = f"""<!doctype html><html lang="en"><head><meta charset="utf-8"><title>Mini-Meet Project Documentation</title><style>
@page {{ size:A4; margin:17mm 16mm 18mm; @bottom-center {{ content:counter(page); font:9pt Arial; color:#666 }} }}
html {{ font:10.5pt/1.45 Calibri,Arial,sans-serif; color:#1b1f24 }} body {{ margin:0 }}
h1 {{ font-size:15pt; color:#14213d; margin:0 0 6pt; break-before:page; break-after:avoid; }}
h1.title {{ break-before:auto; font-size:24pt; margin:4pt 0 14pt }} .course {{ color:#b5560c; font-weight:600; letter-spacing:1px; font-size:10pt }}
h2 {{ font-size:11.5pt; color:#14213d; margin:11pt 0 4pt; break-after:avoid }}
p {{ margin:0 0 6pt }} code {{ font:8.8pt Consolas,monospace; background:#f1f3f5; padding:0 2px }}
.file {{ font:bold 8.6pt Consolas,monospace; color:#4a5568; margin-top:6pt }}
pre {{ font:7.9pt/1.25 Consolas,monospace; background:#f6f7f9; border:1px solid #dde1e6; padding:5pt 7pt; margin:2pt 0 8pt; white-space:pre-wrap }}
table {{ border-collapse:collapse; width:100%; font-size:9.5pt; margin:4pt 0 9pt; break-inside:avoid }}
th,td {{ border:1px solid #cfd5dc; padding:3.5pt 6pt; text-align:left; vertical-align:top }} th {{ background:#e9edf2 }}
td.n,th.n {{ text-align:right }} .done {{ color:#1e7a46; font-weight:700 }} h1 .done {{ font-size:10pt; margin-left:6pt; white-space:nowrap }}
.info th {{ width:24% }} figure {{ margin:6pt 0 10pt; text-align:center; break-inside:avoid }} figure img {{ border:1px solid #dde1e6 }}
figcaption {{ font-size:8.8pt; color:#4a5568; margin-top:3pt }}
</style></head><body>{front}{s1}{s2}{s3}{s4}{s5}</body></html>"""

(ROOT / "docs" / "documentation.html").write_text(PAGE)
print("wrote docs/documentation.html")
