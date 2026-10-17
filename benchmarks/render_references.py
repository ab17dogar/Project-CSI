#!/usr/bin/env python3
"""Render high-sample-count references for the benchmark scenes.

    python3 benchmarks/render_references.py                 # all scenes, 16384 spp
    python3 benchmarks/render_references.py --spp 65536 --scenes veach_mis

Writes benchmarks/references/<scene>/{reference.pfm, variance.pfm,
reference.png, meta.json}. The variance file is the per-pixel variance of
the reference mean. Pass it to the renderer (--ref-variance) or to rt_compare
to remove the reference's own noise from measured errors.

References are rendered with MIS and their own seed (common.REFERENCE_SEED),
never shared with test runs. A reference is a point estimate, not ground
truth; the quality of a reference is limited by its sample count, especially
for caustics (glass_caustic). Final references for a paper should use as
many samples as the hardware allows.
"""
import argparse
import json
import time
from datetime import datetime, timezone

import common


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenes", nargs="*", default=common.SCENES)
    ap.add_argument("--spp", type=int, default=16384)
    ap.add_argument("--threads", type=int, default=0, help="0 = all cores")
    ap.add_argument("--bin", default=None, help="path to the Raytracer binary")
    ap.add_argument("--force", action="store_true", help="overwrite existing references")
    args = ap.parse_args()

    renderer = common.find_renderer(args.bin)
    for scene in args.scenes:
        out = common.REFS_DIR / scene
        meta_path = out / "meta.json"
        if meta_path.is_file() and not args.force:
            with open(meta_path) as f:
                meta = json.load(f)
            if meta.get("spp", 0) >= args.spp:
                print(f"{scene}: reference exists ({meta['spp']} spp), skipping")
                continue
        out.mkdir(parents=True, exist_ok=True)
        cmd = [renderer, "--scene", common.scene_path(scene), "--progressive",
               "--samples", args.spp, "--seed", common.REFERENCE_SEED,
               "--integrator", "mis", "--no-denoise", "--quiet",
               "--out", out / "reference.png", "--pfm", out / "reference.pfm",
               "--variance-out", out / "variance.pfm", "--log", out / "log.csv"]
        if args.threads > 0:
            cmd += ["--threads", args.threads]
        print(f"{scene}: rendering {args.spp} spp ...", flush=True)
        t0 = time.time()
        common.run(cmd)
        wall = time.time() - t0
        rows = common.read_log(out / "log.csv")
        meta = {
            "scene": scene,
            "spp": int(rows[-1]["spp"]),
            "seed": common.REFERENCE_SEED,
            "integrator": "mis",
            "render_seconds": rows[-1]["total_seconds"],
            "wall_seconds": wall,
            "renderer_commit": common.git_commit(),
            "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
            "machine": common.machine_info(),
        }
        with open(meta_path, "w") as f:
            json.dump(meta, f, indent=2)
        print(f"{scene}: done in {wall:.0f} s")


if __name__ == "__main__":
    main()
