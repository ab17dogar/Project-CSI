#!/usr/bin/env python3
"""Overview figure of a make_dataset.py dataset.

    python3 benchmarks/plot_dataset.py datasets/cornell_box [--test-view 0]

Writes <dataset>/overview.png: a few training views, the camera positions
over the initial point cloud (top view), and one test view without and with
the inserted objects, with the object mask, the change the objects make to
the rest of the image, depth and normals. Images are the dataset's own PNGs
(its exposure); the difference is computed in HDR. Requires matplotlib and
numpy.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

from check_dataset import read_points
from plot_convergence import INK, INK_2, MUTED, SURFACE
from plot_renders import read_pfm

TRAIN_COLOR, TEST_COLOR = "#2a78d6", "#eb6834"  # categorical slots 1 and 2


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dataset", type=Path)
    ap.add_argument("--test-view", type=int, default=0)
    ap.add_argument("--gain", type=float, default=8.0, help="amplification of the difference image")
    args = ap.parse_args()
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from PIL import Image
    except ImportError:
        sys.exit("matplotlib and pillow are required")

    d = args.dataset
    meta = json.loads((d / "dataset.json").read_text())
    train = meta["views"]["train"]
    test = meta["views"].get("test", [])
    inserted = "test_inserted" in meta["views"]
    stem = f"r_{args.test_view:03d}"

    def png(path):
        return np.asarray(Image.open(path).convert("RGB"))

    picks = [train[i]["file"] for i in np.linspace(0, len(train) - 1, 4).astype(int)]
    fig = plt.figure(figsize=(16, 8.6), dpi=150, facecolor=SURFACE)
    gs = fig.add_gridspec(2, 6, height_ratios=[1, 1], hspace=0.28, wspace=0.06,
                          left=0.02, right=0.98, top=0.88, bottom=0.03)
    scene = meta["scene"]
    title = f"Input: {scene}" + (f" + {meta['insert']}" if meta.get("insert") else "")
    fig.text(0.02, 0.965, title, color=INK, fontsize=12, va="center")
    fig.text(0.02, 0.935,
             f"{len(train)} training and {len(test)} test views, {meta['spp']} spp each, "
             f"{meta['w']}x{meta['h']}, {meta['poses']} poses, {meta['points']} initial points, "
             f"renderer {meta['renderer_commit']}",
             color=INK_2, fontsize=9, va="center")

    def show(ax, img, label):
        ax.imshow(img, interpolation="nearest")
        ax.set_axis_off()
        ax.set_title(label, color=INK_2, fontsize=8.5, loc="left")

    for k, s in enumerate(picks):
        show(fig.add_subplot(gs[0, k]), png(d / "train" / f"{s}.png"), f"training view {s} (scene only)")

    # Top view, z to the right: points coloured by their stored colour,
    # cameras on top.
    ax = fig.add_subplot(gs[0, 4:])
    pts = np.array(read_points(d / "points3d.ply"))
    raw = (d / "points3d.ply").read_bytes()
    end = raw.index(b"end_header\n") + len(b"end_header\n")
    cols = np.frombuffer(raw[end:], dtype=np.dtype([("p", "<f4", 6), ("c", "u1", 3)]))["c"] / 255.0
    ax.scatter(pts[:, 2], pts[:, 0], c=cols, s=1.2, linewidths=0, rasterized=True)
    for views, color, marker, label in ((train, TRAIN_COLOR, "o", "training cameras"),
                                        (test, TEST_COLOR, "s", "test cameras")):
        if not views:
            continue
        p = np.array([v["look_from"] for v in views])
        a = np.array([v["look_at"] for v in views])
        dirs = a - p
        dirs /= np.linalg.norm(dirs, axis=1, keepdims=True)
        for pi, di in zip(p, dirs):
            ax.plot([pi[2], pi[2] + 0.35 * di[2]], [pi[0], pi[0] + 0.35 * di[0]],
                    color=color, lw=1.0, zorder=3)
        ax.scatter(p[:, 2], p[:, 0], color=color, marker=marker, s=22, edgecolors=SURFACE,
                   linewidths=1.0, zorder=4, label=label)
    ax.set_aspect("equal", adjustable="datalim")
    ax.set_xlabel("z", color=INK_2, fontsize=8)
    ax.set_ylabel("x", color=INK_2, fontsize=8)
    ax.yaxis.tick_right()  # the left side borders the training views
    ax.yaxis.set_label_position("right")
    ax.tick_params(colors=MUTED, labelsize=7)
    for side in ax.spines.values():
        side.set_color(MUTED)
    ax.legend(loc="lower center", bbox_to_anchor=(0.5, 1.0), ncol=2, fontsize=7.5,
              frameon=False, labelcolor=INK)
    ax.set_title("top view: cameras over points3d.ply", color=INK_2, fontsize=8.5, loc="left",
                 pad=18)

    row = []
    row.append((png(d / "test" / f"{stem}.png"), f"test {stem}: scene only"))
    if inserted:
        row.append((png(d / "test_inserted" / f"{stem}.png"), "object inserted: ground truth"))
        row.append((png(d / "test_inserted" / f"{stem}_mask.png"), "object mask"))
        a = read_pfm(d / "test" / f"{stem}.pfm")
        b = read_pfm(d / "test_inserted" / f"{stem}.pfm")
        mask = png(d / "test_inserted" / f"{stem}_mask.png")[..., :1] > 0
        exposure = 2.0 ** meta.get("exposure_stops", 0.0)
        diff = np.clip(0.5 + args.gain * exposure * (b - a), 0, 1)
        diff = np.where(mask, 0.5, diff)  # the sphere itself is shown in the mask panel
        row.append((diff, f"difference x{args.gain:g} (gray = unchanged)"))
    depth = read_pfm(d / "geometry" / "test" / f"{stem}_depth.pfm")[..., 0]
    hit = depth > 0
    dv = np.zeros_like(depth)
    dv[hit] = 1 - (depth[hit] - depth[hit].min()) / (np.ptp(depth[hit]) + 1e-12)
    row.append((dv, "depth (near = light)"))
    normal = read_pfm(d / "geometry" / "test" / f"{stem}_normal.pfm")
    row.append((np.clip(0.5 + 0.5 * normal, 0, 1) * hit[..., None], "world normal (rgb = xyz)"))
    for k, (img, label) in enumerate(row[:6]):
        ax = fig.add_subplot(gs[1, k])
        if img.ndim == 2:
            ax.imshow(img, cmap="gray", vmin=0, vmax=1, interpolation="nearest")
            ax.set_axis_off()
            ax.set_title(label, color=INK_2, fontsize=8.5, loc="left")
        else:
            show(ax, img, label)

    out = d / "overview.png"
    fig.savefig(out, facecolor=SURFACE)
    plt.close(fig)
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
