#!/usr/bin/env python3
"""Equal-time comparison of light-transport methods on the benchmark scenes.

    python3 benchmarks/run_benchmarks.py --time 30
    python3 benchmarks/run_benchmarks.py --scenes veach_mis --methods mis nee --time 60 --repeats 3

For every scene x method x repeat, the renderer runs progressively for the
same render-time budget, logging error against the scene's reference after
every pass (relMSE, MSE, MAPE, HDR-FLIP, with the reference's own noise
subtracted). Outputs, under benchmarks/results/<run>/:

  <scene>/<method>_s<seed>.csv   per-pass convergence log
  <scene>/<method>_s<seed>.png   final image (and .pfm)
  summary.csv                    final-pass metrics of every run
  run.json                       commit, machine, settings

Timing is render time only (no scene loading or BVH build). For fair numbers,
run on an otherwise idle machine and keep --threads fixed across methods.
"""
import argparse
import csv
import json
from datetime import datetime, timezone

import common

# Methods are integrator settings of the renderer, keyed by the label used in
# logs and plots. New techniques get added here.
METHODS = {
    "mis": ["--integrator", "mis"],
    "bsdf": ["--integrator", "bsdf"],
    "nee": ["--integrator", "nee"],
    # Gaussian fields: light selection by the light hierarchy instead of
    # power (no effect on scenes without a field). "tree" uses all of its
    # bounds; the other two are the ablation: distance only (the hierarchy
    # of stage 2b) and distance with the emitter's orientation and SH, but
    # no receiver bound.
    "mis_tree": ["--integrator", "mis", "--field-sampler", "tree"],
    "nee_tree": ["--integrator", "nee", "--field-sampler", "tree"],
    "mis_tree_distance": ["--integrator", "mis", "--field-sampler", "tree-distance"],
    "nee_tree_distance": ["--integrator", "nee", "--field-sampler", "tree-distance"],
    "mis_tree_emitter": ["--integrator", "mis", "--field-sampler", "tree-emitter"],
    "nee_tree_emitter": ["--integrator", "nee", "--field-sampler", "tree-emitter"],
}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenes", nargs="*", default=common.SCENES)
    ap.add_argument("--methods", nargs="*", default=list(METHODS))
    ap.add_argument("--time", type=float, default=30.0, help="render seconds per run")
    ap.add_argument("--repeats", type=int, default=1, help="independent seeds per method")
    ap.add_argument("--threads", type=int, default=0, help="0 = all cores")
    ap.add_argument("--name", default=None, help="results subdirectory (default: timestamp)")
    ap.add_argument("--no-flip", action="store_true", help="skip FLIP (faster logging)")
    ap.add_argument("--bin", default=None)
    args = ap.parse_args()

    for m in args.methods:
        if m not in METHODS:
            raise SystemExit(f"unknown method '{m}' (known: {', '.join(METHODS)})")
    if not 1 <= args.repeats <= 999:
        raise SystemExit("--repeats must be between 1 and 999")

    renderer = common.find_renderer(args.bin)
    name = args.name or datetime.now().strftime("%Y%m%d-%H%M%S")
    out_root = common.RESULTS_DIR / name
    out_root.mkdir(parents=True, exist_ok=True)

    run_info = {
        "created": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "renderer_commit": common.git_commit(),
        "machine": common.machine_info(),
        "time_budget": args.time,
        "threads": args.threads or common.machine_info()["logical_cores"],
        "methods": {m: METHODS[m] for m in args.methods},
        "references": {},
    }

    summary = []
    for scene_arg in args.scenes:
        scene_file, scene = common.scene_path(scene_arg), common.scene_name(scene_arg)
        ref_meta = common.load_reference_meta(scene)
        run_info["references"][scene] = ref_meta
        ref_dir = common.REFS_DIR / scene
        scene_out = out_root / scene
        scene_out.mkdir(exist_ok=True)
        for method in args.methods:
            for seed in range(1, args.repeats + 1):
                if seed == ref_meta["seed"]:
                    raise SystemExit("test seed collides with the reference seed")
                stem = scene_out / f"{method}_s{seed}"
                cmd = [renderer, "--scene", scene_file,
                       "--time-budget", args.time, "--seed", seed,
                       "--reference", ref_dir / "reference.pfm",
                       "--ref-variance", ref_dir / "variance.pfm",
                       "--log", f"{stem}.csv", "--label", method,
                       "--no-denoise", "--quiet",
                       "--out", f"{stem}.png", "--pfm", f"{stem}.pfm"]
                cmd += METHODS[method]
                if args.threads > 0:
                    cmd += ["--threads", args.threads]
                if args.no_flip:
                    cmd.append("--no-flip")
                print(f"{scene:15s} {method:6s} seed {seed}: ", end="", flush=True)
                common.run(cmd)
                last = common.read_log(f"{stem}.csv")[-1]
                summary.append({"scene": scene, "method": method, "seed": seed, **{
                    k: last[k] for k in ("spp", "total_seconds", "mrays_per_s", "mse",
                                         "relmse", "mse_corrected", "relmse_corrected",
                                         "mape", "flip")}})
                print(f"{int(last['spp'])} spp, relMSE {last['relmse_corrected']:.3e}"
                      + (f", FLIP {last['flip']:.4f}" if last.get("flip") is not None else ""))

    with open(out_root / "summary.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
        w.writeheader()
        w.writerows(summary)
    with open(out_root / "run.json", "w") as f:
        json.dump(run_info, f, indent=2)
    print(f"\nResults in {out_root}")
    print(f"Plot with: python3 benchmarks/plot_convergence.py {out_root}")


if __name__ == "__main__":
    main()
