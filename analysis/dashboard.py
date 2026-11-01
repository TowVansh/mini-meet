"""Call-quality dashboard: live stats of a running call, plus the evaluation results.

    bin/mm --server HOST --room demo --name PC1 --csv results/live-pc1.csv
    python3 analysis/dashboard.py --csv results/live-pc1.csv     # http://localhost:8090
    python3 analysis/dashboard.py --selftest

Standard library only. The client appends one CSV row per stream per second;
the page polls that file through /api/live.
"""

import argparse
import csv
import io
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

RESULTS = Path(__file__).resolve().parent.parent / "results"
TAIL_BYTES = 400_000  # about the last few minutes of a call
SERIES_POINTS = 120

PAGE = r"""<!doctype html><html><head><meta charset="utf-8"><title>Mini-Meet Dashboard</title>
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{--bg:#f3f5f8;--card:#fff;--ink:#14213d;--muted:#5b6779;--line:#d8dde4;--a:#e0782b;--b:#2f5bb5}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 "Segoe UI",Arial,sans-serif}
header{background:var(--ink);color:#fff;padding:14px 24px;display:flex;align-items:baseline;gap:16px}
header h1{font-size:20px;margin:0}header span{color:#c9d3e3;font-size:13px}
main{padding:18px 24px;max-width:1280px;margin:auto}h2{font-size:17px;margin:20px 0 10px}
.peer{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:14px;margin-bottom:14px}
.peer h3{margin:0 0 10px;font-size:16px}.peer h3 small{color:var(--muted);font-weight:400;margin-left:8px}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(112px,1fr));gap:10px}
.s b{display:block;font-size:22px;font-variant-numeric:tabular-nums}.s{color:var(--muted);font-size:12px}.s b{color:var(--ink)}
.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:12px;margin-top:12px}
canvas{width:100%;height:130px;display:block;border:1px solid var(--line);border-radius:6px}
.cap{font-size:12px;color:var(--muted);margin-bottom:3px}
.wrap{overflow-x:auto;background:var(--card);border:1px solid var(--line);border-radius:8px}
table{border-collapse:collapse;width:100%;font-size:13px}th,td{padding:5px 9px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th:first-child,td:first-child{text-align:left}th{background:#e9edf2}
select{font:inherit;padding:4px;margin-bottom:8px}#tl{max-width:100%;background:#fff;border:1px solid var(--line);border-radius:8px}
.none{color:var(--muted);padding:12px 0}
</style></head><body>
<header><h1>Mini-Meet Dashboard</h1><span id="src"></span></header><main>
<h2>Live call</h2><div id="live"><div class="none">Waiting for call data...</div></div>
<h2>Evaluation results (28 tc netem runs)</h2><div class="wrap"><table id="sum"></table></div>
<h2>Run timeline</h2><select id="pick"></select><br><img id="tl" alt="timeline of the selected run">
</main><script>
const $=i=>document.getElementById(i), n=(v,d=0)=>v==null?"-":Number(v).toFixed(d);
function draw(c,series,colors){const r=c.getBoundingClientRect(),d=devicePixelRatio||1;c.width=r.width*d;c.height=r.height*d;
 const x=c.getContext("2d");x.scale(d,d);const W=r.width,H=r.height,all=series.flat().filter(v=>v!=null),max=Math.max(1,...all)*1.1;
 x.fillStyle="#5b6779";x.font="11px sans-serif";x.fillText(n(max),4,12);
 series.forEach((s,i)=>{x.strokeStyle=colors[i];x.lineWidth=2;x.beginPath();let on=false;
  s.forEach((v,j)=>{if(v==null)return;const px=W*j/Math.max(1,s.length-1),py=H-4-(H-20)*v/max;on?x.lineTo(px,py):x.moveTo(px,py);on=true});x.stroke()})}
function stat(l,v){return `<div class="s"><b>${v}</b>${l}</div>`}
async function live(){const d=await (await fetch("/api/live")).json();$("src").textContent=d.file||"no live CSV given";
 const names=Object.keys(d.peers);if(!names.length)return;
 names.forEach(p=>{const e=d.peers[p],id="p_"+p.replace(/\W/g,"_");let el=$(id);
  if(!el){if($("live").querySelector(".none"))$("live").innerHTML="";el=document.createElement("div");el.className="peer";el.id=id;
   el.innerHTML=`<h3></h3><div class="stats"></div><div class="charts"><div><div class="cap">Bitrate kbps: received (blue), sent (orange)</div><canvas></canvas></div><div><div class="cap">RTT ms</div><canvas></canvas></div><div><div class="cap">Received fps</div><canvas></canvas></div><div><div class="cap">Packet loss % received</div><canvas></canvas></div></div>`;$("live").appendChild(el)}
  const L=e.latest;el.querySelector("h3").innerHTML=`${p}<small>${L.path||""}, updated ${L.age}s ago</small>`;
  el.querySelector(".stats").innerHTML=stat("RTT ms",n(L.rtt))+stat("Video in kbps",n(L.vin))+stat("Video out kbps",n(L.vout))+stat("Received fps",n(L.fps))+stat("Resolution",L.res)+stat("Loss %",n(L.loss,1))+stat("Jitter ms",n(L.jitter,1))+stat("Freezes",n(L.freezes))+stat("Audio concealed %",n(L.conceal,1))+stat("Playout spread ms",n(L.sd,1))+stat("Rate level",n(L.level));
  const c=el.querySelectorAll("canvas");draw(c[0],[e.vin,e.vout],["#2f5bb5","#e0782b"]);draw(c[1],[e.rtt],["#e0782b"]);draw(c[2],[e.fps],["#2f5bb5"]);draw(c[3],[e.loss],["#b23a48"])})}
async function results(){const d=await (await fetch("/api/summary")).json();if(!d.length)return;
 const cols=[["profile","Profile"],["adapt","Rate control"],["video_kbps","Video kbps"],["video_fps","fps"],["video_loss_pct","Loss %"],["jitter_ms","Jitter ms"],["rtt_ms","RTT ms"],["freezes","Freezes"],["audio_conceal_pct","Audio concealed %"],["playout_sd_ms","Playout spread ms"]];
 $("sum").innerHTML="<tr>"+cols.map(c=>`<th>${c[1]}</th>`).join("")+"</tr>"+d.map(r=>"<tr>"+cols.map(c=>`<td>${c[0]=="adapt"?(r.adapt=="1"?"on":"off"):c[0]=="profile"?r.profile:n(r[c[0]],1)}</td>`).join("")+"</tr>").join("");
 const t=await (await fetch("/api/timelines")).json();$("pick").innerHTML=t.map(x=>`<option>${x}</option>`).join("");
 $("pick").value=t.includes("schedule")?"schedule":t[0];$("pick").onchange=()=>$("tl").src=`/results/timeline_${$("pick").value}.png`;$("pick").onchange()}
results();live();setInterval(live,1000);
</script></body></html>"""


def num(v):
    try:
        return float(v)
    except (TypeError, ValueError):
        return None


def parse_live(text, now_ms):
    """Group CSV rows by remote peer: the latest values and per-second series."""
    peers = {}
    for r in csv.DictReader(io.StringIO(text)):
        if not r.get("ts") or not r.get("remote") or r.get("kind") != "video":
            continue
        p = peers.setdefault(r["remote"], {"vin": [], "vout": [], "rtt": [], "fps": [], "loss": [], "latest": {}})
        L = p["latest"]
        L["age"] = max(0, round((now_ms - num(r["ts"])) / 1000))
        L["path"], L["rtt"], L["level"] = r["candidate_type"], num(r["rtt_ms"]), num(r["adapt_level"])
        if r["direction"] == "in":
            p["vin"].append(num(r["bitrate_kbps"]))
            p["rtt"].append(num(r["rtt_ms"]))
            p["fps"].append(num(r["fps"]))
            p["loss"].append(num(r["loss_pct"]))
            L.update(vin=num(r["bitrate_kbps"]), fps=num(r["fps"]), loss=num(r["loss_pct"]), jitter=num(r["jitter_ms"]),
                     freezes=num(r["freeze_count"]), res=f'{r["width"]}x{r["height"]}', sd=num(r.get("playout_sd_ms")))
        else:
            p["vout"].append(num(r["bitrate_kbps"]))
            L["vout"] = num(r["bitrate_kbps"])
    for p in peers.values():
        for k in ("vin", "vout", "rtt", "fps", "loss"):
            p[k] = p[k][-SERIES_POINTS:]
    return peers


def read_live(path):
    """The whole header, plus the end of the file (a long call must not slow the poll)."""
    if not path or not os.path.exists(path):
        return ""
    with open(path, "rb") as f:
        header = f.readline()
        size = f.seek(0, os.SEEK_END)
        start = max(len(header), size - TAIL_BYTES)
        f.seek(start)
        body = f.read()
    if start > len(header):
        body = body.split(b"\n", 1)[-1]  # drop the partial first line
    return (header + body).decode("utf-8", "replace")


def add_audio(peers, text):
    for r in csv.DictReader(io.StringIO(text)):
        if r.get("kind") == "audio" and r.get("direction") == "in" and r.get("remote") in peers:
            peers[r["remote"]]["latest"]["conceal"] = num(r["concealed_pct"])


class Handler(BaseHTTPRequestHandler):
    live_csv = None

    def send(self, body, ctype="application/json", code=200):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        import time
        path = self.path.split("?")[0]
        if path == "/":
            self.send(PAGE.encode(), "text/html; charset=utf-8")
        elif path == "/api/live":
            text = read_live(self.live_csv)
            peers = parse_live(text, time.time() * 1000)
            add_audio(peers, text)
            self.send(json.dumps({"file": self.live_csv, "peers": peers}).encode())
        elif path == "/api/summary":
            f = RESULTS / "summary.csv"
            rows = list(csv.DictReader(f.open())) if f.exists() else []
            self.send(json.dumps(rows).encode())
        elif path == "/api/timelines":
            names = sorted(p.stem[len("timeline_"):] for p in RESULTS.glob("timeline_*.png"))
            self.send(json.dumps(names).encode())
        elif path.startswith("/results/"):
            # basename only: the request can never leave results/
            f = RESULTS / os.path.basename(path)
            if f.suffix == ".png" and f.exists():
                self.send(f.read_bytes(), "image/png")
            else:
                self.send(b"not found", "text/plain", 404)
        else:
            self.send(b"not found", "text/plain", 404)

    def log_message(self, *args):
        pass


def selftest():
    text = ("ts,remote,kind,direction,bitrate_kbps,loss_pct,jitter_ms,rtt_ms,fps,width,height,concealed_pct,"
            "freeze_count,adapt_level,candidate_type,playout_sd_ms\n"
            "1000,PC2,video,in,500,1.5,3,20,20,640,480,,2,1,host->host,1.2\n"
            "1000,PC2,video,out,600,0,1,20,,,,,,1,host->host,\n"
            "1000,PC2,audio,in,30,0,1,20,,,,4.5,,1,host->host,\n")
    peers = parse_live(text, 3000)
    add_audio(peers, text)
    L = peers["PC2"]["latest"]
    assert L["vin"] == 500 and L["vout"] == 600 and L["res"] == "640x480" and L["age"] == 2 and L["conceal"] == 4.5
    assert peers["PC2"]["vin"] == [500] and peers["PC2"]["vout"] == [600]
    assert parse_live("", 0) == {}
    print("dashboard selftest ok")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", help="stats file of the running call (mm --csv)")
    ap.add_argument("--port", type=int, default=8090)
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    Handler.live_csv = a.csv
    print(f"Dashboard on http://localhost:{a.port}  live csv: {a.csv}")
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
