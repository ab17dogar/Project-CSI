"""Shared helpers for the benchmark scripts (standard library only)."""
import json
import os
import platform
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "benchmarks"
SCENES_DIR = BENCH / "scenes"
REFS_DIR = BENCH / "references"
RESULTS_DIR = BENCH / "results"

SCENES = ["cornell_box", "cornell_glossy", "veach_mis", "glass_caustic",
          "env_objects", "sun_sky_bunny"]

# References use their own seed range. A test render that shares sample
# streams with its reference is correlated with it, and its measured error
# drops faster than 1/N. Test runs use seeds 1..999.
REFERENCE_SEED = 1_000_003


def find_renderer(explicit=None):
    candidates = [explicit] if explicit else [
        ROOT / "build" / "Raytracer", ROOT / "build" / "Release" / "Raytracer.exe",
        ROOT / "build" / "Raytracer.exe"]
    for c in candidates:
        if c and Path(c).is_file():
            return str(c)
    sys.exit("Raytracer binary not found; build first or pass --bin")


def git_commit():
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"],
                             capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "-C", str(ROOT), "status", "--porcelain", "--", "src"],
                               capture_output=True, text=True).stdout.strip()
        return out + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def machine_info():
    cpu = platform.processor() or "unknown"
    try:
        with open("/proc/cpuinfo") as f:
            for line in f:
                if line.startswith("model name"):
                    cpu = line.split(":", 1)[1].strip()
                    break
    except OSError:
        pass
    return {"cpu": cpu, "logical_cores": os.cpu_count(), "system": platform.platform()}


def scene_path(name):
    p = SCENES_DIR / f"{name}.xml"
    if not p.is_file():
        sys.exit(f"unknown scene '{name}' (no {p})")
    return p


def load_reference_meta(scene):
    meta_path = REFS_DIR / scene / "meta.json"
    if not meta_path.is_file():
        sys.exit(f"no reference for '{scene}'. Run benchmarks/render_references.py first.")
    with open(meta_path) as f:
        return json.load(f)


def run(cmd, quiet=True):
    if not quiet:
        print("  $", " ".join(str(c) for c in cmd))
    proc = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        sys.exit(f"command failed ({proc.returncode}): {' '.join(str(c) for c in cmd)}")
    return proc


def read_log(path):
    """Rows of a renderer --log CSV (comment lines skipped), numbers parsed."""
    import csv
    with open(path) as f:
        lines = [l for l in f if not l.startswith("#")]
    rows = []
    for r in csv.DictReader(lines):
        parsed = {}
        for k, v in r.items():
            if k in ("scene", "method"):
                parsed[k] = v
            else:
                parsed[k] = float(v) if v not in ("", None) else None
        rows.append(parsed)
    return rows
