#!/usr/bin/env python3
"""Side-by-side image comparison for a benchmark run.

    python3 benchmarks/plot_renders.py benchmarks/results/<run> [--seed 1]

Writes <run>/plots/renders.png and renders.pdf: one row per scene, the
reference followed by each method's final equal-time render, captioned with
its sample count and relMSE.

Images are tone mapped from the HDR .pfm files (exposure, clamp, sRGB), with
one exposure per scene shared by every column, so the columns differ only in
the method. Pixels are shown 1:1 (2x2 in the PNG); no filtering, which would
hide the noise being compared. Needs the .pfm renders and references, which
are not in git: run render_references.py and run_benchmarks.py first.
Requires matplotlib and numpy.
"""
import argparse
import csv
import sys
from pathlib import Path

import numpy as np

import common
from plot_convergence import INK, INK_2, METHOD_STYLE, MUTED, SURFACE, style_for

# Exposure in stops per scene, chosen once for display. The metrics are
# computed on the HDR images and do not depend on it.
EXPOSURE_STOPS = {
    "cornell_box": 1.5,
    "cornell_glossy": 1.5,
    "veach_mis": 1.0,
    "sun_sky_bunny": -1.0,
}


def read_pfm(path):
    with open(path, "rb") as f:
        if f.readline().strip() != b"PF":
            raise ValueError(f"{path}: not an RGB PFM")
        width, height = map(int, f.readline().split())
        scale = float(f.readline())
        data = np.frombuffer(f.read(), dtype="<f4" if scale < 0 else ">f4")
    # PFM rows run bottom to top.
    return data.reshape(height, width, 3)[::-1]


def to_display(img, stops):
    x = np.clip(img * 2.0 ** stops, 0.0, 1.0)
    return np.where(x <= 0.0031308, 12.92 * x, 1.055 * np.power(x, 1 / 2.4) - 0.055)


def read_summary(path):
    with open(path, newline="") as f:
        return {(r["scene"], r["method"], int(r["seed"])): r for r in csv.DictReader(f)}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--seed", type=int, default=1, help="which seed's renders to show")
    args = ap.parse_args()

    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.lines import Line2D
    except ImportError:
        sys.exit("matplotlib is required: pip install matplotlib")

    summary = read_summary(args.run_dir / "summary.csv")
    scenes = [s for s in common.SCENES if (args.run_dir / s).is_dir()]
    known = list(METHOD_STYLE)
    found = {m for (_, m, _) in summary}
    methods = [m for m in known if m in found] + sorted(found - set(known))

    rows = []
    for scene in scenes:
        ref_dir = common.REFS_DIR / scene
        ref_meta = common.load_reference_meta(scene)
        stops = EXPOSURE_STOPS.get(scene, 0.0)
        cells = [(to_display(read_pfm(ref_dir / "reference.pfm"), stops),
                  f"{ref_meta.get('spp', '?')} spp")]
        for m in methods:
            r = summary.get((scene, m, args.seed))
            pfm = args.run_dir / scene / f"{m}_s{args.seed}.pfm"
            if r is None or not pfm.exists():
                cells.append((None, "missing"))
                continue
            cells.append((to_display(read_pfm(pfm), stops),
                          f"{int(float(r['spp']))} spp, relMSE {float(r['relmse_corrected']):.2e}"))
        rows.append((scene, stops, cells))

    # Layout in pixels at 100 dpi: every column slot is as wide as the widest
    # image, so the column headers line up with every row.
    slot_w = max(c[0].shape[1] for _, _, cells in rows for c in cells if c[0] is not None)
    gap, margin, title_h, header_h, label_h, caption_h, row_gap = 10, 20, 44, 30, 22, 24, 10
    ncol = 1 + len(methods)
    width = 2 * margin + ncol * slot_w + (ncol - 1) * gap
    heights = [label_h + max(c[0].shape[0] for c in cells if c[0] is not None) + caption_h
               for _, _, cells in rows]
    height = title_h + header_h + sum(heights) + row_gap * (len(rows) - 1) + margin
    dpi = 100
    fig = plt.figure(figsize=(width / dpi, height / dpi), dpi=dpi, facecolor=SURFACE)

    def fx(px):
        return px / width

    def fy(px_from_top):
        return 1.0 - px_from_top / height

    fig.text(fx(margin), fy(26), f"Equal-time renders, seed {args.seed}, next to the reference",
             color=INK, fontsize=12, va="center")

    columns = [("Reference", MUTED, None)] + [
        (style_for(m, i)["label"], style_for(m, i)["color"], style_for(m, i)["marker"])
        for i, m in enumerate(methods)]
    # Column headers: a legend-style swatch and the label, centered on the slot.
    renderer = fig.canvas.get_renderer()
    for c, (label, color, marker) in enumerate(columns):
        y = title_h + header_h / 2
        text = fig.text(0, fy(y), label, color=INK, fontsize=10, va="center")
        x0 = margin + c * (slot_w + gap)
        x0 += (slot_w - 28 - text.get_window_extent(renderer).width) / 2
        text.set_x(fx(x0 + 28))
        fig.add_artist(Line2D([fx(x0), fx(x0 + 11), fx(x0 + 22)], [fy(y)] * 3, color=color, lw=1.6,
                              marker=marker, markevery=[1] if marker else None, ms=4.5,
                              mec=SURFACE, mew=1.2, transform=fig.transFigure))

    top = title_h + header_h
    for (scene, stops, cells), row_h in zip(rows, heights):
        label = scene.replace("_", " ")
        if stops:
            label += f"   (display exposure {stops:+g} stops)"
        fig.text(fx(margin), fy(top + label_h / 2), label, color=INK_2, fontsize=9, va="center")
        img_h = row_h - label_h - caption_h
        for c, (img, caption) in enumerate(cells):
            x0 = margin + c * (slot_w + gap)
            if img is not None:
                h, w = img.shape[:2]
                left = x0 + (slot_w - w) / 2
                ax = fig.add_axes([fx(left), fy(top + label_h + h), fx(w), h / height])
                ax.imshow(img, interpolation="nearest")
                ax.set_axis_off()
            fig.text(fx(x0 + slot_w / 2), fy(top + label_h + img_h + caption_h / 2), caption,
                     color=INK_2, fontsize=8, ha="center", va="center")
        top += row_h + row_gap

    out = args.run_dir / "plots"
    out.mkdir(exist_ok=True)
    fig.savefig(out / "renders.png", dpi=2 * dpi, facecolor=SURFACE)
    fig.savefig(out / "renders.pdf", dpi=2 * dpi, facecolor=SURFACE)
    plt.close(fig)
    # The figure is opaque: drop the alpha channel and recompress.
    from PIL import Image
    Image.open(out / "renders.png").convert("RGB").save(out / "renders.png", optimize=True)
    print(f"Wrote {out / 'renders.png'} and .pdf")


if __name__ == "__main__":
    main()
