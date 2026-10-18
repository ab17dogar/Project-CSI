#!/usr/bin/env python3
"""Error of a benchmark run on surfaces and on what the camera sees directly.

    python3 benchmarks/region_metrics.py benchmarks/results/<run> \\
        [--scene-file name=path/to/scene.xml ...]

Light sampling only matters where a path reaches geometry. In scenes with a
Gaussian field most of the image can be the field seen directly, where every
strategy does the same thing, and whole-image errors understate the
difference between methods. This splits each image into

  surfaces   pixels whose camera ray hits geometry (depth > 0)
  direct     the rest: the field (or the environment) seen directly

using the renderer's --geometry-out, and reports relMSE (as in the metrics
library: per channel, (t - r)^2 / (r^2 + 0.01), minus the reference's own
variance) per region, method and seed, plus the mean over seeds and the
ratio to MIS. Needs the final .pfm renders of the run and the references.
Writes <run>/plots/regions.md and regions.csv. Requires numpy.
"""
import argparse
import csv
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

import numpy as np

import common
from plot_renders import read_pfm

REL_EPS = 0.01


def relmse(test, ref, var, mask):
    norm = ref ** 2 + REL_EPS
    e = ((test - ref) ** 2 - var) / norm
    return float(e[mask].mean()) if mask.any() else None


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run_dir", type=Path)
    ap.add_argument("--scene-file", action="append", default=[], metavar="NAME=PATH",
                    help="scene file of a run scene that is not in the suite")
    ap.add_argument("--bin", help="renderer binary")
    args = ap.parse_args()
    overrides = dict(s.split("=", 1) for s in args.scene_file)
    renderer = common.find_renderer(args.bin)

    rows = []
    for scene_dir in sorted(p for p in args.run_dir.iterdir() if p.is_dir() and p.name != "plots"):
        scene = scene_dir.name
        if scene in overrides:
            scene_file = Path(overrides[scene]).resolve()
        elif (common.SCENES_DIR / f"{scene}.xml").is_file():
            scene_file = common.SCENES_DIR / f"{scene}.xml"
        else:
            hits = list((common.ROOT / "datasets").glob(f"*/{scene}.xml"))
            if not hits:
                sys.exit(f"{scene}: scene file not found; pass --scene-file {scene}=PATH")
            scene_file = hits[0]
        ref_dir = common.REFS_DIR / scene
        ref = read_pfm(ref_dir / "reference.pfm").astype(np.float64)
        var = read_pfm(ref_dir / "variance.pfm").astype(np.float64)
        width = int(ET.parse(scene_file).getroot().find("Config/Width").get("value"))
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([renderer, "--scene", str(scene_file), "--samples", "1",
                            "--width", str(width), "--no-denoise", "--quiet",
                            "--out", f"{tmp}/g.png", "--geometry-out", f"{tmp}/geo"], check=True)
            depth = read_pfm(Path(tmp) / "geo_depth.pfm")[..., 0]
        if depth.shape != ref.shape[:2]:
            sys.exit(f"{scene}: geometry {depth.shape} and reference {ref.shape[:2]} differ")
        regions = {"surfaces": depth > 0, "direct": depth <= 0}
        for pfm in sorted(scene_dir.glob("*_s*.pfm")):
            m = re.fullmatch(r"(.+)_s(\d+)", pfm.stem)
            test = read_pfm(pfm).astype(np.float64)
            for region, mask in regions.items():
                rows.append({"scene": scene, "method": m.group(1), "seed": int(m.group(2)),
                             "region": region, "pixels": int(mask.sum()),
                             "relmse_corrected": relmse(test, ref, var, mask[..., None].repeat(3, -1))})

    out = args.run_dir / "plots"
    out.mkdir(exist_ok=True)
    with open(out / "regions.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)

    mean = defaultdict(list)
    for r in rows:
        if r["relmse_corrected"] is not None:
            mean[(r["scene"], r["region"], r["method"])].append(r["relmse_corrected"])
    lines = ["| scene | region | pixels | method | relMSE | relMSE / MIS |", "|---|---|---:|---|---:|---:|"]
    for scene, region in sorted({(k[0], k[1]) for k in mean}):
        pixels = next(r["pixels"] for r in rows if r["scene"] == scene and r["region"] == region)
        base = np.mean(mean.get((scene, region, "mis"), [np.nan]))
        for method in [m for m in ("mis", "bsdf", "nee", "mis_tree", "nee_tree")
                       if (scene, region, m) in mean]:
            v = float(np.mean(mean[(scene, region, method)]))
            lines.append(f"| {scene} | {region} | {pixels} | {method} | {v:.3e} | {v / base:.2f} |")
    (out / "regions.md").write_text("\n".join(lines) + "\n\nrelMSE corrected for reference noise, "
                                    "mean over seeds; per channel as in the metrics library.\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
