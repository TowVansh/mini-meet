"""Collect the numbers the slide deck charts, from results/ (run inside WSL/Linux).

    python3 docs/slides/deck_data.py    ->  docs/slides/deck-data.json
"""

import json
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
RES = ROOT / "results"

summary = pd.read_csv(RES / "summary.csv")


def row(profile, adapt):
    r = summary[(summary.profile == profile) & (summary.adapt == adapt)].iloc[0]
    # numpy scalars -> plain Python so json can write them
    return {k: (None if pd.isna(v) else (v.item() if hasattr(v, "item") else v)) for k, v in r.items()}


def schedule(adapt):
    """Per-second send bitrate and RTT of botA's video in the changing-conditions call."""
    run = f"eval_schedule_adapt{adapt}_p2"
    df = pd.read_csv(RES / f"{run}__botA.csv")
    t0 = df.ts.min()
    out = df[(df.kind == "video") & (df.direction == "out")].copy()
    out["t"] = ((out.ts - t0) / 1000).round().astype(int)
    out = out.groupby("t").agg(kbps=("bitrate_kbps", "mean"), rtt=("rtt_ms", "mean"))
    phases = []
    for line in (RES / f"{run}.phases").read_text().split("\n"):
        if line.strip():
            ms, name = line.split()
            phases.append({"t": round((int(ms) - t0) / 1000), "name": name})
    return out, phases


s0, ph0 = schedule(0)
s1, ph1 = schedule(1)
n = int(min(s0.index.max(), s1.index.max()))
secs = list(range(0, n + 1))
data = {
    "profiles": {p: {"off": row(p, 0), "on": row(p, 1)} for p in summary.profile.unique()},
    "schedule": {
        "t": secs,
        "kbps_off": [round(float(s0.kbps.get(t, float("nan"))), 0) if t in s0.index else None for t in secs],
        "kbps_on": [round(float(s1.kbps.get(t, float("nan"))), 0) if t in s1.index else None for t in secs],
        "rtt_off_s": [round(float(s0.rtt.get(t)) / 1000, 2) if t in s0.index else None for t in secs],
        "rtt_on_s": [round(float(s1.rtt.get(t)) / 1000, 2) if t in s1.index else None for t in secs],
        "phases": ph1,
    },
}
(ROOT / "docs" / "slides" / "deck-data.json").write_text(json.dumps(data, indent=1))
print("wrote deck-data.json:", len(secs), "seconds,", len(data["profiles"]), "profiles")
