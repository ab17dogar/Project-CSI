#!/usr/bin/env python3
"""Insert a dataset's objects into a Gaussian scene and score it per region.

    python3 benchmarks/points_to_gaussians.py datasets/cornell_box
    python3 benchmarks/insertion_demo.py datasets/cornell_box --view 1 --spp 256

Renders one test view twice through the path tracer: the Gaussian scene
alone (default <dataset>/proxy.ply, or any trained 3DGS .ply with --field)
and with the objects of <dataset>/insert.xml added. The field is the only
light. Both are compared with the path-traced ground truth (test/ and
test_inserted/) in three regions:

  object      pixels where an inserted object is visible (its mask)
  influence   other pixels the objects change in the ground truth (their
              shadow and the light they reflect), |delta| > --threshold
  rest        everything else

Writes <dataset>/demo/: both renders (.pfm, .png), metrics.json and
insertion.png. Requires numpy and matplotlib, except with --render-only.
"""
import argparse
import json
import math
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

import common
from make_dataset import merge_insert

REL_EPS = 0.01  # relMSE denominator offset, as in the metrics library


def relmse(a, b, mask):
    """Mean and median of the per-pixel relative squared error. A few
    pixels can dominate the mean (an emitter that moved), so report both."""
    import numpy as np
    if not mask.any():
        return None
    lum = 0.2126 * b[..., 0] + 0.7152 * b[..., 1] + 0.0722 * b[..., 2]
    e = ((a - b) ** 2).mean(axis=-1) / (lum ** 2 + REL_EPS)
    return {"mean": float(e[mask].mean()), "median": float(np.median(e[mask]))}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dataset", type=Path)
    ap.add_argument("--field", type=Path, help="Gaussian .ply (default <dataset>/proxy.ply)")
    ap.add_argument("--view", type=int, default=1, help="test view index")
    ap.add_argument("--spp", type=int, default=256)
    ap.add_argument("--max-depth", type=int, default=8)
    ap.add_argument("--threshold", type=float, default=0.05,
                    help="influence region: relative luminance change in the ground truth")
    ap.add_argument("--bin", help="renderer binary")
    ap.add_argument("--render-only", action="store_true", help="skip metrics and figure")
    ap.add_argument("--reuse", action="store_true", help="reuse existing renders in <dataset>/demo")
    args = ap.parse_args()

    d = args.dataset.resolve()
    meta = json.loads((d / "dataset.json").read_text())
    field = (args.field or d / "proxy.ply").resolve()
    if not field.is_file():
        sys.exit(f"no Gaussian scene at {field}; run points_to_gaussians.py first")
    view = meta["views"]["test"][args.view]
    stem = view["file"]
    out = d / "demo"
    out.mkdir(exist_ok=True)
    renderer = common.find_renderer(args.bin)

    aspect = math.tan(meta["camera_angle_x"] / 2) / math.tan(meta["camera_angle_y"] / 2)
    base = ET.fromstring(f"""<ItemContainer>
  <Config><Width value="{meta['w']}"/><Aspect_ratio value="{aspect!r}"/>
    <Samples_Per_Pixel value="{args.spp}"/><Max_Depth value="{args.max_depth}"/></Config>
  <Camera><Look_From x="0" y="0" z="1"/><Look_at x="0" y="0" z="0"/><Up x="0" y="1" z="0"/>
    <FOV angle="{meta['vertical_fov']}"/></Camera>
  <Lights><Sun><Direction x="0" y="1" z="0"/><Intensity value="0"/><Color r="1" g="1" b="1"/></Sun></Lights>
  <Materials/>
  <Objects><GaussianField file="{field}"/></Objects>
</ItemContainer>""")
    scenes = {"field": base,
              "field_inserted": merge_insert(base, ET.parse(d / "insert.xml").getroot())}
    pfms = {}
    for name, root in scenes.items():
        xml = out / f"{name}.xml"
        ET.ElementTree(root).write(xml, encoding="unicode", xml_declaration=True)
        pfm = out / f"{name}_{stem}.pfm"
        pfms[name] = pfm
        if args.reuse and pfm.is_file():
            continue
        cmd = [renderer, "--scene", xml, "--samples", args.spp, "--width", meta["w"],
               "--look-from", ",".join(map(repr, view["look_from"])),
               "--look-at", ",".join(map(repr, view["look_at"])),
               "--up", ",".join(map(repr, meta["up"])), "--fov", meta["vertical_fov"],
               "--seed", view["seed"], "--no-denoise", "--quiet",
               "--exposure", meta.get("exposure_stops", 0.0),
               "--out", out / f"{name}_{stem}.png", "--pfm", pfm]
        print(f"rendering {name} ({args.spp} spp) ...", flush=True)
        subprocess.run([str(c) for c in cmd], check=True)
    if args.render_only:
        return

    import numpy as np
    from plot_convergence import INK, INK_2, SURFACE
    from plot_renders import read_pfm, to_display
    renders = {name: read_pfm(p).astype(np.float64) for name, p in pfms.items()}

    gt = read_pfm(d / "test" / f"{stem}.pfm").astype(np.float64)
    gt_ins = read_pfm(d / "test_inserted" / f"{stem}.pfm").astype(np.float64)
    from PIL import Image
    obj = np.asarray(Image.open(d / "test_inserted" / f"{stem}_mask.png")) > 0
    lum = lambda x: 0.2126 * x[..., 0] + 0.7152 * x[..., 1] + 0.0722 * x[..., 2]
    change = np.abs(lum(gt_ins) - lum(gt)) / (lum(gt) + REL_EPS)
    influence = (change > args.threshold) & ~obj
    rest = ~obj & ~influence
    regions = {"object": obj, "influence": influence, "rest": rest}

    metrics = {"view": stem, "spp": args.spp, "field": str(field), "threshold": args.threshold,
               "pixels": {k: int(v.sum()) for k, v in regions.items()},
               "relmse": {
                   # The Gaussian scene against the scene it reconstructs.
                   "field_vs_scene": {k: relmse(renders["field"], gt, m) for k, m in regions.items()},
                   # The insertion against the ground truth insertion.
                   "inserted_vs_truth": {k: relmse(renders["field_inserted"], gt_ins, m)
                                         for k, m in regions.items()},
                   # Ignoring the objects' effect on the scene altogether.
                   "scene_vs_truth": {k: relmse(gt, gt_ins, m) for k, m in regions.items()},
               }}
    (out / "metrics.json").write_text(json.dumps(metrics, indent=2))
    print(json.dumps(metrics["relmse"], indent=2))

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    stops = meta.get("exposure_stops", 0.0)
    region_img = np.full(gt.shape, 0.92)
    region_img[influence] = (0.92, 0.41, 0.20)  # slot 2
    region_img[obj] = (0.16, 0.47, 0.84)        # slot 1
    panels = [(to_display(gt, stops), "ground truth: scene"),
              (to_display(renders["field"], stops), "Gaussian scene alone"),
              (to_display(gt_ins, stops), "ground truth: object inserted"),
              (to_display(renders["field_inserted"], stops), "Gaussian scene + object (stage 1)"),
              (region_img, "regions: object (blue), influence (orange)")]
    fig, axes = plt.subplots(1, 5, figsize=(16, 4.5), dpi=150, facecolor=SURFACE)
    for ax, (img, label) in zip(axes, panels):
        ax.imshow(img, interpolation="nearest")
        ax.set_axis_off()
        ax.set_title(label, color=INK_2, fontsize=9, loc="left")
    r = metrics["relmse"]
    fmt = lambda v: "-" if v is None else f"{v['mean']:.3g} ({v['median']:.2g})"
    rows = [("Gaussian scene vs scene", r["field_vs_scene"]),
            ("insertion vs ground truth", r["inserted_vs_truth"]),
            ("ground truth without object", r["scene_vs_truth"])]
    lines = [f"{'relMSE: mean (median)':28s}{'object':>21}{'influence':>21}{'rest':>21}"]
    for label, row in rows:
        lines.append(f"{label:28s}" + "".join(f"{fmt(row[k]):>21}" for k in ("object", "influence", "rest")))
    fig.text(0.01, 0.03, "\n".join(lines), color=INK, fontsize=8, family="monospace", va="bottom")
    fig.subplots_adjust(left=0.01, right=0.99, top=0.9, bottom=0.2, wspace=0.04)
    fig.savefig(out / "insertion.png", facecolor=SURFACE)
    print(f"Wrote {out / 'insertion.png'}")


if __name__ == "__main__":
    main()
