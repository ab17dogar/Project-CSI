#!/usr/bin/env python3
"""Multi-view datasets for reconstructing a scene, with and without inserted objects.

    python3 benchmarks/make_dataset.py cornell_box \\
        --insert benchmarks/inserts/cornell_blue_sphere.xml \\
        --poses jitter --train 100 --test 20 --spp 512

Path traces a scene from many viewpoints and writes a dataset in the NeRF /
Blender "transforms" layout, which standard 3D Gaussian Splatting trainers
read directly. Under --out (default datasets/<scene>/):

  transforms_train.json   camera-to-world poses and intrinsics of train/
  transforms_test.json    the same for test/ (and test_inserted/)
  train/r_000.png ...     training views, 8-bit sRGB at a fixed exposure
  train/r_000.pfm ...     the same views in linear HDR
  test/                   held-out views of the unmodified scene
  points3d.ply            initial point cloud: back-projected training depth
  geometry/<split>/r_000_depth.pfm, r_000_normal.pfm
                          z-depth and world normal of the first hit
  dataset.json            renderer commit, settings and every pose

benchmarks/check_dataset.py verifies the poses against the rendered geometry.

With --insert FILE (an <ItemContainer> holding <Materials> and <Objects>):

  test_inserted/r_000.png / .pfm   ground truth: the test views with the
                                   objects inserted, path traced
  test_inserted/r_000_mask.png     where an inserted object is visible
  insert.xml                       copy of FILE

Training sees only the unmodified scene; test_inserted is what a method
that inserts the objects into the reconstruction must reproduce, shadows and
interreflections included (docs/DATASETS.md, docs/RESEARCH_DIRECTION.md C4).

Every view has its own render seed, so noise is independent across views
(one seed for all would repeat the same noise at each pixel position in every
view). A test_inserted view reuses the seed of its test view: the pair shares
random numbers, so their difference is far less noisy than between
independent renders (docs/DATASETS.md has a measurement).
Relative paths inside FILE resolve against the base scene's directory.
Standard library only.
"""
import argparse
import json
import math
import os
import random
import shutil
import struct
import sys
import time
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

import common


# --- scene file -------------------------------------------------------------

def vec_attr(elem, tag):
    e = elem.find(tag)
    if e is None:
        sys.exit(f"scene camera has no <{tag}>")
    return [float(e.get(k)) for k in ("x", "y", "z")]


def scene_camera(root):
    cam = root.find("Camera")
    cfg = root.find("Config")
    if cam is None or cfg is None:
        sys.exit("scene needs <Camera> and <Config>")
    fov = cam.find("FOV")
    return {
        "look_from": vec_attr(cam, "Look_From"),
        "look_at": vec_attr(cam, "Look_at"),
        "up": vec_attr(cam, "Up"),
        "fov": float(fov.get("angle")) if fov is not None else 40.0,
        "width": int(float(cfg.find("Width").get("value"))),
        "aspect": float(cfg.find("Aspect_ratio").get("value")),
    }


def merge_insert(base_root, insert_root):
    """Append the insert's materials and objects to a copy of the scene."""
    merged = ET.fromstring(ET.tostring(base_root))
    existing = {m.get("name") for m in merged.iter() if m.get("name")}
    for section in ("Materials", "Objects", "Lights"):
        src = insert_root.find(section)
        if src is None:
            continue
        dst = merged.find(section)
        if dst is None:
            dst = ET.SubElement(merged, section)
        for child in src:
            name = child.get("name")
            if section == "Materials" and name in existing:
                sys.exit(f"inserted material '{name}' already exists in the scene")
            dst.append(child)
    return merged


# --- poses ------------------------------------------------------------------

def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def add(a, b):
    return [a[i] + b[i] for i in range(3)]


def scale(a, s):
    return [x * s for x in a]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def normalize(a):
    n = math.sqrt(dot(a, a))
    return scale(a, 1.0 / n)


def camera_basis(look_from, look_at, up):
    """The renderer's camera frame (camera.cpp): looks along -w."""
    w = normalize(sub(look_from, look_at))
    u = normalize(cross(up, w))
    v = cross(w, u)
    return u, v, w


def orbit_poses(n, center, radius, elev_min, elev_max, phase):
    """Fibonacci spiral over a band of elevations, all looking at center."""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    s0, s1 = math.sin(math.radians(elev_min)), math.sin(math.radians(elev_max))
    poses = []
    for i in range(n):
        y = s0 + (s1 - s0) * (i + 0.5) / n
        r = math.sqrt(max(0.0, 1.0 - y * y))
        phi = golden * i + phase
        d = [r * math.cos(phi), y, r * math.sin(phi)]
        poses.append((add(center, scale(d, radius)), list(center)))
    return poses


def jitter_poses(n, look_from, look_at, amount, rng):
    """Perturb the scene's own camera: for closed rooms one cannot orbit."""
    dist = math.sqrt(dot(sub(look_from, look_at), sub(look_from, look_at)))

    def ball(radius):
        while True:
            p = [rng.uniform(-1, 1) for _ in range(3)]
            if dot(p, p) <= 1.0:
                return scale(p, radius)

    return [(add(look_from, ball(amount * dist)), add(look_at, ball(0.3 * amount * dist)))
            for _ in range(n)]


# --- image files ------------------------------------------------------------

def read_pfm(path):
    """(width, height, rows top first, each a list of (r, g, b))."""
    with open(path, "rb") as f:
        if f.readline().strip() != b"PF":
            sys.exit(f"{path}: not an RGB PFM")
        w, h = map(int, f.readline().split())
        endian = "<" if float(f.readline()) < 0 else ">"
        data = struct.unpack(f"{endian}{w * h * 3}f", f.read(w * h * 12))
    rows = []
    for y in range(h - 1, -1, -1):  # PFM stores the bottom row first
        base = y * w * 3
        rows.append([data[base + 3 * x: base + 3 * x + 3] for x in range(w)])
    return w, h, rows


def write_png_gray(path, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)
    h, w = len(rows), len(rows[0])

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def srgb8(x, exposure):
    x = min(max(x * exposure, 0.0), 1.0)
    x = 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
    return min(255, int(256 * x))


def write_points_ply(path, points):
    with open(path, "wb") as f:
        f.write(("ply\nformat binary_little_endian 1.0\n"
                 f"element vertex {len(points)}\n"
                 "property float x\nproperty float y\nproperty float z\n"
                 "property float nx\nproperty float ny\nproperty float nz\n"
                 "property uchar red\nproperty uchar green\nproperty uchar blue\n"
                 "end_header\n").encode())
        for p, n, c in points:
            f.write(struct.pack("<6f3B", *p, *n, *c))


# --- main -------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scene", help="benchmark scene name or path to a scene XML")
    ap.add_argument("--insert", type=Path, help="XML with <Materials>/<Objects> to insert")
    ap.add_argument("--out", type=Path, help="output directory (default datasets/<scene>)")
    ap.add_argument("--train", type=int, default=100, help="training views")
    ap.add_argument("--test", type=int, default=20, help="test views")
    ap.add_argument("--spp", type=int, default=512, help="samples per pixel per view")
    ap.add_argument("--width", type=int, help="image width (default: the scene's)")
    ap.add_argument("--poses", choices=["orbit", "jitter"], default="orbit")
    ap.add_argument("--center", type=float, nargs=3, help="orbit center (default: the scene's look-at)")
    ap.add_argument("--radius", type=float, help="orbit radius (default: the scene's camera distance)")
    ap.add_argument("--elev", type=float, nargs=2, default=(10.0, 60.0), metavar=("MIN", "MAX"),
                    help="orbit elevation band in degrees")
    ap.add_argument("--jitter", type=float, default=0.15,
                    help="jitter radius as a fraction of the camera distance")
    ap.add_argument("--exposure", type=float, default=0.0, help="PNG exposure in stops")
    ap.add_argument("--points", type=int, default=100000, help="points in points3d.ply")
    ap.add_argument("--seed", type=int, default=1, help="pose and render seed (each view derives its own)")
    ap.add_argument("--threads", type=int, default=0, help="0 = all cores")
    ap.add_argument("--bin", help="renderer binary")
    ap.add_argument("--force", action="store_true", help="overwrite an existing --out")
    args = ap.parse_args()

    scene = Path(args.scene)
    if not scene.is_file():
        scene = common.scene_path(args.scene)
    scene = scene.resolve()
    out = (args.out or common.ROOT / "datasets" / scene.stem).resolve()
    if out.exists():
        if not args.force:
            sys.exit(f"{out} exists (use --force to overwrite)")
        shutil.rmtree(out)
    renderer = common.find_renderer(args.bin)

    base_root = ET.parse(scene).getroot()
    cam = scene_camera(base_root)
    width = args.width or cam["width"]
    rng = random.Random(args.seed)
    n_total = args.train + args.test
    if args.poses == "orbit":
        center = list(args.center) if args.center else cam["look_at"]
        radius = args.radius or math.sqrt(dot(sub(cam["look_from"], cam["look_at"]),
                                              sub(cam["look_from"], cam["look_at"])))
        # One spiral, interleaved: test views fall between training views.
        spiral = orbit_poses(n_total, center, radius, *args.elev, phase=rng.uniform(0, 2 * math.pi))
        # Spacing n_total / test >= 1, so the indices are distinct.
        test_idx = {int((i + 0.5) * n_total / args.test) for i in range(args.test)}
        train_poses = [p for i, p in enumerate(spiral) if i not in test_idx]
        test_poses = [p for i, p in enumerate(spiral) if i in test_idx]
    else:
        poses = jitter_poses(n_total, cam["look_from"], cam["look_at"], args.jitter, rng)
        train_poses, test_poses = poses[:args.train], poses[args.train:]

    inserted_scene = None
    if args.insert:
        merged = merge_insert(base_root, ET.parse(args.insert).getroot())
        # Next to the base scene, so relative asset paths still resolve.
        inserted_scene = scene.with_name(f".make_dataset_{os.getpid()}_{scene.name}")
        ET.ElementTree(merged).write(inserted_scene, encoding="unicode", xml_declaration=True)

    up = cam["up"]
    fov = cam["fov"]
    t0 = time.time()
    meta_frames = {}
    try:
        def view_seed(split, i):
            first = 0 if split == "train" else args.train  # test_inserted = test
            # Clear of benchmark test seeds (1..999) and common.REFERENCE_SEED.
            return 5_000_000 + args.seed * 100_000 + first + i

        def render(scene_file, split, i, pose):
            (out / split).mkdir(parents=True, exist_ok=True)
            geo = out / "geometry" / split
            geo.mkdir(parents=True, exist_ok=True)
            stem = f"r_{i:03d}"
            look_from, look_at = pose
            cmd = [renderer, "--scene", scene_file, "--samples", args.spp, "--width", width,
                   "--look-from", ",".join(map(repr, look_from)),
                   "--look-at", ",".join(map(repr, look_at)),
                   "--up", ",".join(map(repr, up)), "--fov", fov,
                   "--seed", view_seed(split, i), "--no-denoise", "--quiet",
                   "--exposure", args.exposure,
                   "--out", out / split / f"{stem}.png", "--pfm", out / split / f"{stem}.pfm",
                   "--geometry-out", geo / stem]
            if args.threads:
                cmd += ["--threads", args.threads]
            common.run(cmd)
            return stem

        jobs = [("train", scene, train_poses), ("test", scene, test_poses)]
        if inserted_scene:
            jobs.append(("test_inserted", inserted_scene, test_poses))
        done, total = 0, sum(len(p) for _, _, p in jobs)
        for split, scene_file, poses in jobs:
            meta_frames[split] = []
            for i, pose in enumerate(poses):
                stem = render(scene_file, split, i, pose)
                meta_frames[split].append((stem, pose, view_seed(split, i)))
                done += 1
                print(f"\r{done}/{total} views ({time.time() - t0:.0f} s)", end="", flush=True)
        print()
    finally:
        if inserted_scene and inserted_scene.exists():
            inserted_scene.unlink()

    # Intrinsics. The renderer maps the full vertical field of view to the
    # image height and aspect * that to the width, so pixels are square only
    # when width / height == aspect; fl_x and fl_y are exact either way.
    w_px, h_px, _ = read_pfm(out / "train" / "r_000.pfm") if train_poses else read_pfm(out / "test" / "r_000.pfm")
    tan_v = math.tan(math.radians(fov) / 2)
    tan_h = cam["aspect"] * tan_v
    intrinsics = {
        "camera_angle_x": 2 * math.atan(tan_h),
        "camera_angle_y": 2 * math.atan(tan_v),
        "fl_x": w_px / (2 * tan_h), "fl_y": h_px / (2 * tan_v),
        "cx": w_px / 2, "cy": h_px / 2, "w": w_px, "h": h_px,
    }

    def c2w(pose):
        u, v, w = camera_basis(pose[0], pose[1], up)
        o = pose[0]
        return [[u[0], v[0], w[0], o[0]], [u[1], v[1], w[1], o[1]],
                [u[2], v[2], w[2], o[2]], [0.0, 0.0, 0.0, 1.0]]

    for split in ("train", "test"):
        frames = [{"file_path": f"./{split}/{stem}", "transform_matrix": c2w(pose)}
                  for stem, pose, _ in meta_frames.get(split, [])]
        with open(out / f"transforms_{split}.json", "w") as f:
            json.dump({**intrinsics, "frames": frames}, f, indent=2)

    # Object masks: the inserted objects are visible where the first hit
    # moved closer to the camera (or exists where the base scene has none).
    if inserted_scene:
        for stem, _, _ in meta_frames["test_inserted"]:
            _, _, d0 = read_pfm(out / "geometry" / "test" / f"{stem}_depth.pfm")
            _, _, d1 = read_pfm(out / "geometry" / "test_inserted" / f"{stem}_depth.pfm")
            mask = [[255 if b[0] > 0 and (a[0] <= 0 or b[0] < a[0] * (1 - 1e-6)) else 0
                     for a, b in zip(r0, r1)] for r0, r1 in zip(d0, d1)]
            write_png_gray(out / "test_inserted" / f"{stem}_mask.png", mask)
        shutil.copy(args.insert, out / "insert.xml")

    # points3d.ply: back-project a subset of training pixels. 3DGS reads it
    # instead of initializing randomly in a unit cube.
    points = []
    if train_poses and args.points > 0:
        per_view = max(1, args.points // len(train_poses))
        prng = random.Random(args.seed + 1)
        vh = 2 * tan_v
        vw = 2 * tan_h
        for stem, pose, _ in meta_frames["train"]:
            _, _, depth = read_pfm(out / "geometry" / "train" / f"{stem}_depth.pfm")
            _, _, normal = read_pfm(out / "geometry" / "train" / f"{stem}_normal.pfm")
            _, _, rgb = read_pfm(out / "train" / f"{stem}.pfm")
            u, v, w = camera_basis(pose[0], pose[1], up)
            f = scale(w, -1.0)
            exposure = 2.0 ** args.exposure
            for _ in range(per_view):
                x, y = prng.randrange(w_px), prng.randrange(h_px)
                z = depth[y][x][0]
                if z <= 0:
                    continue
                s = (x + 0.5) / w_px - 0.5
                t = (h_px - 1 - y + 0.5) / h_px - 0.5
                d = add(f, add(scale(u, s * vw), scale(v, t * vh)))
                p = add(pose[0], scale(d, z))
                c = tuple(srgb8(ch, exposure) for ch in rgb[y][x])
                points.append((p, normal[y][x], c))
        write_points_ply(out / "points3d.ply", points)

    meta = {
        "scene": str(scene.relative_to(common.ROOT)) if scene.is_relative_to(common.ROOT) else str(scene),
        "insert": str(args.insert) if args.insert else None,
        "renderer_commit": common.git_commit(),
        "machine": common.machine_info(),
        "created": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "spp": args.spp, "seed": args.seed, "exposure_stops": args.exposure,
        "poses": args.poses, "up": up, "vertical_fov": fov, **intrinsics,
        "points": len(points), "render_seconds": round(time.time() - t0, 1),
        "views": {split: [{"file": stem, "look_from": p[0], "look_at": p[1], "seed": seed}
                          for stem, p, seed in views]
                  for split, views in meta_frames.items()},
    }
    with open(out / "dataset.json", "w") as f:
        json.dump(meta, f, indent=2)
    print(f"Wrote {out}")


if __name__ == "__main__":
    main()
