"""Summarise and plot Mini-Meet impairment runs.

    python3 analysis/plot.py results/<tag>_*.csv

Run names follow <tag>_<profile>_adapt<0|1>_p<peers>; each client writes its
own file <run>__<name>.csv. Writes into results/:
    summary.csv, summary.md          one row per run
    bars_<metric>.png                adaptation off vs on, per profile
    timeline_<profile>.png           per-second traces for one profile
"""

import re
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import pandas as pd  # noqa: E402

SKIP_SECONDS = 15  # ignore call setup + clean warm-up (impairment starts ~11 s in)
RUN_RE = re.compile(r"_(?P<profile>[a-z0-9]+)_adapt(?P<adapt>[01])_p(?P<peers>\d)(__|$)")
OUT = Path(__file__).resolve().parent.parent / "results"


def load(paths):
    frames = []
    for p in paths:
        df = pd.read_csv(p)
        if df.empty:
            continue
        m = RUN_RE.search(Path(p).stem)
        df["profile"] = m["profile"] if m else Path(p).stem
        df["adapt"] = int(m["adapt"]) if m else df["adapt_on"].iloc[0]
        df["t"] = (df["ts"] - df["ts"].min()) / 1000.0
        frames.append(df)
    if not frames:
        sys.exit("no data")
    return pd.concat(frames, ignore_index=True)


def summarise(df):
    steady = df[df["t"] >= SKIP_SECONDS]
    rows = []
    for (run, profile, adapt), g in steady.groupby(["run", "profile", "adapt"]):
        vin = g[(g.kind == "video") & (g.direction == "in")]
        ain = g[(g.kind == "audio") & (g.direction == "in")]
        vout = g[(g.kind == "video") & (g.direction == "out")]
        rows.append({
            "run": run,
            "profile": profile,
            "adapt": adapt,
            "video_kbps": vin.bitrate_kbps.median(),
            "video_fps": vin.fps.median(),
            "video_height": vin.height.median(),
            "video_loss_pct": vin.loss_pct.mean(),
            "jitter_ms": vin.jitter_ms.median(),
            "rtt_ms": g.rtt_ms.median(),
            "freezes": vin.groupby("peer").freeze_count.max().sum(),
            "audio_conceal_pct": ain.concealed_pct.mean(),
            "bwe_kbps": vout.avail_out_kbps.median(),
            "adapt_level": vout.adapt_level.mean(),
            "top_limit": vout.limit_reason.mode().iloc[0] if vout.limit_reason.notna().any() else "",
            # jitter buffer: spread of shown-frame intervals, and the delay the buffer adds
            "playout_sd_ms": vin["playout_sd_ms"].median() if "playout_sd_ms" in vin else float("nan"),
            "audio_jb_ms": ain["jb_delay_ms"].median() if "jb_delay_ms" in ain else float("nan"),
        })
    order = {p: i for i, p in enumerate(
        "baseline loss1 loss5 loss10 loss20 burst5 delay100 jitter30 jitter60 rate2m rate1m rate500k rate250k combo".split())}
    s = pd.DataFrame(rows)
    s["_o"] = s.profile.map(order).fillna(99)
    return s.sort_values(["_o", "adapt"]).drop(columns="_o")


def to_markdown(s):
    cols = ["profile", "adapt", "video_kbps", "video_fps", "video_height", "video_loss_pct",
            "jitter_ms", "rtt_ms", "freezes", "audio_conceal_pct", "playout_sd_ms", "audio_jb_ms",
            "bwe_kbps", "top_limit"]
    lines = ["| " + " | ".join(cols) + " |", "|" + "---|" * len(cols)]
    for _, r in s[cols].iterrows():
        cells = [f"{v:.1f}" if isinstance(v, float) else str(v) for v in r]
        lines.append("| " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def bars(s, metric, label):
    pivot = s.pivot_table(index="profile", columns="adapt", values=metric, sort=False)
    ax = pivot.plot.bar(figsize=(10, 4), rot=30)
    ax.set_ylabel(label)
    ax.set_xlabel("")
    ax.set_title(f"{label} by impairment profile")
    ax.legend(["adaptation off", "adaptation on"][: len(pivot.columns)])
    plt.tight_layout()
    plt.savefig(OUT / f"bars_{metric}.png", dpi=130)
    plt.close()


def phases(df, run):
    """Impairment changes recorded by bench/run-matrix.sh: [(t seconds, profile)]."""
    f = OUT / f"{run}.phases"
    if not f.exists():
        return []
    t0 = df[df.run == run].ts.min()
    out = []
    for line in f.read_text().split("\n"):
        if line.strip():
            ms, name = line.split()
            out.append(((int(ms) - t0) / 1000.0, name))
    return out


def timeline(df, profile):
    sub = df[df.profile == profile]
    vid = sub[sub.kind == "video"]
    if vid.empty:
        return
    fig, axes = plt.subplots(5, 1, figsize=(10, 11), sharex=True)
    for adapt, g in vid.groupby("adapt"):
        out = g[g.direction == "out"].groupby("t").mean(numeric_only=True)
        inn = g[g.direction == "in"].groupby("t").mean(numeric_only=True)
        aud = sub[(sub.adapt == adapt) & (sub.kind == "audio") & (sub.direction == "in")].groupby("t").mean(numeric_only=True)
        tag = f"adapt {'on' if adapt else 'off'}"
        axes[0].plot(out.index, out.bitrate_kbps, label=f"send kbps ({tag})")
        axes[0].plot(out.index, out.avail_out_kbps, "--", label=f"target kbps ({tag})")
        axes[1].plot(inn.index, inn.fps, label=tag)
        axes[2].plot(inn.index, inn.loss_pct, label=tag)
        axes[3].plot(out.index, out.rtt_ms, label=tag)
        if "jb_delay_ms" in aud:
            axes[4].plot(aud.index, aud.jb_delay_ms, label=f"audio buffer ({tag})")
        if "playout_sd_ms" in inn:
            axes[4].plot(inn.index, inn.playout_sd_ms, ":", label=f"video playout sd ({tag})")
    marks = phases(df, vid.run.iloc[-1])
    for ax, y in zip(axes, ["kbps", "fps (recv)", "loss % (recv)", "RTT ms", "jitter buffer ms"]):
        ax.set_ylabel(y)
        if marks:
            for t, _ in marks[1:]:
                ax.axvline(t, color="grey", lw=0.8, ls=":")
        else:
            ax.axvline(SKIP_SECONDS, color="grey", lw=0.8, ls=":")
        ax.legend(fontsize=7)
    for t, name in marks:
        axes[0].annotate(name, (t, 1.0), xycoords=("data", "axes fraction"), xytext=(3, -12),
                         textcoords="offset points", fontsize=8, color="dimgrey")
    axes[-1].set_xlabel("seconds since call start")
    fig.suptitle(f"Profile: {profile}")
    plt.tight_layout()
    plt.savefig(OUT / f"timeline_{profile}.png", dpi=130)
    plt.close()


def main():
    paths = sys.argv[1:] or sorted(str(p) for p in OUT.glob("*.csv") if p.name != "summary.csv")
    df = load(paths)
    # a changing-conditions run mixes profiles, so it gets a timeline but no summary row
    s = summarise(df[df.profile != "schedule"])
    OUT.mkdir(exist_ok=True)
    s.to_csv(OUT / "summary.csv", index=False)
    (OUT / "summary.md").write_text(to_markdown(s))
    for metric, label in [("video_kbps", "Received video bitrate (kbps)"), ("video_fps", "Received fps"),
                          ("video_loss_pct", "Video packet loss (%)"), ("freezes", "Video freezes"),
                          ("audio_conceal_pct", "Audio concealment (%)"), ("rtt_ms", "RTT (ms)"),
                          ("playout_sd_ms", "Video playout interval spread (ms)"),
                          ("audio_jb_ms", "Audio jitter-buffer delay (ms)")]:
        bars(s, metric, label)
    for profile in df.profile.unique():
        timeline(df, profile)
    print(to_markdown(s))
    print(f"wrote {OUT}/summary.csv, summary.md and PNGs")


if __name__ == "__main__":
    main()
