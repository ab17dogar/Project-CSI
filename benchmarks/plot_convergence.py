#!/usr/bin/env python3
"""Convergence plots and a summary table for a benchmark run.

    python3 benchmarks/plot_convergence.py benchmarks/results/<run>

Writes into <run>/plots/:
  convergence_relmse.png / .pdf   relMSE vs render time, one panel per scene
  convergence_flip.png   / .pdf   HDR-FLIP vs render time
  summary.md                      final errors, error ratio vs MIS, throughput

relMSE is the reference-noise-corrected value. Points where the correction
reaches zero or below (the error is under the reference's own noise floor) are
dropped from the log-scale plot and flagged in the table. Requires matplotlib
(pip install matplotlib); everything else in benchmarks/ is standard library.
"""
import argparse
import math
import sys
from collections import defaultdict
from pathlib import Path

import common

# Fixed method -> categorical slot mapping, so a method keeps its color in
# every figure regardless of which methods a run includes. Validated as a
# set (all pairs, light surface) with the dataviz palette checker.
METHOD_STYLE = {
    "mis": {"color": "#2a78d6", "marker": "o", "label": "MIS"},
    "bsdf": {"color": "#eb6834", "marker": "s", "label": "BSDF sampling"},
    "nee": {"color": "#1baf7a", "marker": "^", "label": "Light sampling (NEE)"},
}
FALLBACK_COLORS = ["#eda100", "#e87ba4", "#008300", "#6250d6", "#e34948"]
SURFACE, INK, INK_2, MUTED = "#fcfcfb", "#0b0b0b", "#52514e", "#898781"
GRID, AXIS = "#e1e0d9", "#c3c2b7"


def style_for(method, index):
    if method in METHOD_STYLE:
        return METHOD_STYLE[method]
    return {"color": FALLBACK_COLORS[index % len(FALLBACK_COLORS)],
            "marker": "D", "label": method}


def load_run(run_dir):
    """{scene: {method: [rows of seed 1, rows of seed 2, ...]}}"""
    data = defaultdict(lambda: defaultdict(list))
    for csv_path in sorted(run_dir.glob("*/*.csv")):
        rows = common.read_log(csv_path)
        if rows:
            data[rows[0]["scene"]][rows[0]["method"]].append(rows)
    return data


def curve(runs, key):
    """Mean of log(metric) across seeds on a shared log-time grid."""
    series = []
    for rows in runs:
        pts = [(r["total_seconds"], r[key]) for r in rows
               if r.get(key) is not None and r[key] > 0 and r["total_seconds"] > 0]
        if len(pts) >= 2:
            series.append(pts)
    if not series:
        return [], []
    if len(series) == 1:
        return [p[0] for p in series[0]], [p[1] for p in series[0]]
    lo = max(s[0][0] for s in series)
    hi = min(s[-1][0] for s in series)
    if hi <= lo:
        return [p[0] for p in series[0]], [p[1] for p in series[0]]
    grid = [math.exp(math.log(lo) + (math.log(hi) - math.log(lo)) * i / 23) for i in range(24)]

    def interp(s, t):
        lt = math.log(t)
        for (t0, e0), (t1, e1) in zip(s, s[1:]):
            if t0 <= t <= t1:
                f = (lt - math.log(t0)) / (math.log(t1) - math.log(t0))
                return math.exp((1 - f) * math.log(e0) + f * math.log(e1))
        return s[-1][1]

    means = [math.exp(sum(math.log(interp(s, t)) for s in series) / len(series)) for t in grid]
    return grid, means


def plot_metric(plt, data, key, ylabel, out_base, title):
    scenes = [s for s in common.SCENES if s in data] + sorted(set(data) - set(common.SCENES))
    cols = min(3, len(scenes))
    rows_n = math.ceil(len(scenes) / cols)
    fig, axes = plt.subplots(rows_n, cols, figsize=(4.1 * cols, 3.3 * rows_n + 0.6),
                             squeeze=False, facecolor=SURFACE)
    methods_seen = []
    for ax, scene in zip(axes.flat, scenes):
        ax.set_facecolor(SURFACE)
        ends = []
        for i, method in enumerate(sorted(data[scene], key=lambda m: list(METHOD_STYLE).index(m)
                                          if m in METHOD_STYLE else 99)):
            st = style_for(method, i)
            t, e = curve(data[scene][method], key)
            if not t:
                continue
            if method not in methods_seen:
                methods_seen.append(method)
            # Markers are a secondary (print / color-vision) channel, not data
            # density: about eight per curve.
            ax.plot(t, e, color=st["color"], lw=1.6, marker=st["marker"], ms=4.5,
                    mec=SURFACE, mew=1.2, markevery=max(1, len(t) // 8),
                    label=st["label"], zorder=3)
            ends.append((e[-1], t[-1], st["label"]))
        # Monte Carlo reference slope: error proportional to 1/time.
        # Drawn below every curve so it never hides one.
        if ends:
            e_end, t_end, _ = min(ends)
            e0 = e_end * 0.35
            ts = [t_end / 30, t_end]
            ax.plot(ts, [e0 * 30, e0], color=MUTED, lw=0.8, zorder=1)
            ax.annotate("1/t", (ts[0], e0 * 30), xytext=(-3, 0), textcoords="offset points",
                        ha="right", va="center", color=MUTED, fontsize=7)
        # Direct labels at line ends, skipped where they would collide (the
        # legend still identifies every series).
        placed = []
        for e_end, t_end, label in sorted(ends):
            ly = math.log10(e_end)
            if all(abs(ly - p) > 0.12 for p in placed):
                ax.annotate(label.split(" (")[0], (t_end, e_end), xytext=(4, 0),
                            textcoords="offset points", va="center", fontsize=7, color=INK_2)
                placed.append(ly)
        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_title(scene.replace("_", " "), color=INK, fontsize=9, loc="left")
        ax.grid(True, which="major", color=GRID, lw=0.6, ls="-")
        ax.set_axisbelow(True)
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        for side in ("bottom", "left"):
            ax.spines[side].set_color(AXIS)
        ax.tick_params(colors=INK_2, labelsize=7, which="both")
        ax.set_xlabel("render time (s)", color=INK_2, fontsize=8)
        ax.set_ylabel(ylabel, color=INK_2, fontsize=8)
        ax.margins(x=0.12)
    for ax in list(axes.flat)[len(scenes):]:
        ax.set_visible(False)
    handles = [plt.Line2D([], [], color=style_for(m, i)["color"], marker=style_for(m, i)["marker"],
                          lw=1.6, ms=5, mec=SURFACE, label=style_for(m, i)["label"])
               for i, m in enumerate(methods_seen)]
    fig.legend(handles=handles, loc="upper center", ncol=len(handles), frameon=False,
               fontsize=8, labelcolor=INK, bbox_to_anchor=(0.5, 1.0))
    fig.suptitle(title, color=INK, fontsize=10, y=1.05)
    fig.tight_layout(rect=(0, 0, 1, 0.96))
    for ext in ("png", "pdf"):
        fig.savefig(f"{out_base}.{ext}", dpi=200, facecolor=SURFACE, bbox_inches="tight")
    plt.close(fig)


def write_summary(data, path):
    lines = ["| scene | method | spp | time (s) | Mrays/s | relMSE | relMSE / MIS | HDR-FLIP |",
             "|---|---|---:|---:|---:|---:|---:|---:|"]
    for scene in [s for s in common.SCENES if s in data] + sorted(set(data) - set(common.SCENES)):
        finals = {}
        for method, runs in data[scene].items():
            last = [r[-1] for r in runs]
            n = len(last)
            avg = lambda k: sum(x[k] for x in last if x.get(k) is not None) / max(
                1, sum(1 for x in last if x.get(k) is not None))
            finals[method] = {k: avg(k) for k in ("spp", "total_seconds", "mrays_per_s",
                                                  "relmse_corrected", "flip")}
            finals[method]["n"] = n
        base = finals.get("mis", {}).get("relmse_corrected")
        for method in sorted(finals, key=lambda m: list(METHOD_STYLE).index(m)
                             if m in METHOD_STYLE else 99):
            f = finals[method]
            rel = f["relmse_corrected"]
            rel_s = f"{rel:.3e}" if rel > 0 else "below ref. noise"
            ratio = f"{rel / base:.2f}" if base and base > 0 and rel > 0 else "-"
            flip = f"{f['flip']:.4f}" if f["flip"] else "-"
            lines.append(f"| {scene} | {style_for(method, 0)['label']} | {f['spp']:.0f} | "
                         f"{f['total_seconds']:.1f} | {f['mrays_per_s']:.1f} | {rel_s} | "
                         f"{ratio} | {flip} |")
    lines += ["", "relMSE is corrected for reference noise; relMSE / MIS < 1 means lower error "
              "than MIS at equal render time. Values are means over seeds."]
    path.write_text("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    args = ap.parse_args()
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is required for plotting: pip install matplotlib")

    data = load_run(args.run_dir)
    if not data:
        sys.exit(f"no logs found under {args.run_dir}")
    out = args.run_dir / "plots"
    out.mkdir(exist_ok=True)
    plot_metric(plt, data, "relmse_corrected", "relMSE", out / "convergence_relmse",
                "Equal-time convergence: relative MSE (lower is better)")
    plot_metric(plt, data, "flip", "HDR-FLIP", out / "convergence_flip",
                "Equal-time convergence: HDR-FLIP (lower is better)")
    write_summary(data, out / "summary.md")
    print(f"wrote {out}/convergence_relmse.png, convergence_flip.png, summary.md")


if __name__ == "__main__":
    main()
