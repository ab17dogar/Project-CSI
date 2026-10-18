#!/usr/bin/env python3
"""An unoptimized Gaussian stand-in for a scene, built from a dataset.

    python3 benchmarks/points_to_gaussians.py datasets/cornell_box --stride 3

Turns every --stride-th pixel of every training view into one flat Gaussian
on the surface it sees (position from the depth map, orientation from the
normal map, size from the pixel footprint, radiance from the HDR view) and
writes a 3DGS-format .ply (default <dataset>/proxy.ply). No optimization:
view-dependent effects are baked in per view, and overlapping discs from
different views simply composite. It stands in for a trained capture so the
whole insertion loop runs without a GPU; a 3DGS model trained on the same
dataset replaces it one to one. Standard library only.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

from make_dataset import read_pfm

C0 = 0.28209479177387814


def quat_from_frame(t1, t2, n):
    """Unit quaternion (w, x, y, z) of the rotation with columns t1, t2, n."""
    m = [[t1[0], t2[0], n[0]], [t1[1], t2[1], n[1]], [t1[2], t2[2], n[2]]]
    tr = m[0][0] + m[1][1] + m[2][2]
    if tr > 0:
        s = 2 * math.sqrt(tr + 1)
        return (0.25 * s, (m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s)
    i = max(range(3), key=lambda k: m[k][k])
    j, k = (i + 1) % 3, (i + 2) % 3
    s = 2 * math.sqrt(1 + m[i][i] - m[j][j] - m[k][k])
    q = [0.0, 0.0, 0.0, 0.0]
    q[0] = (m[k][j] - m[j][k]) / s
    q[1 + i] = 0.25 * s
    q[1 + j] = (m[j][i] + m[i][j]) / s
    q[1 + k] = (m[k][i] + m[i][k]) / s
    return tuple(q)


def tangent_frame(n, view):
    """Right-handed frame (t1, t2, n), t1 along the view direction's
    projection onto the surface (any tangent if the view is head-on)."""
    dn = sum(view[k] * n[k] for k in range(3))
    t1 = tuple(view[k] - dn * n[k] for k in range(3))
    l = math.sqrt(sum(c * c for c in t1))
    if l < 1e-6:
        a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
        t1 = (a[1] * n[2] - a[2] * n[1], a[2] * n[0] - a[0] * n[2], a[0] * n[1] - a[1] * n[0])
        l = math.sqrt(sum(c * c for c in t1))
    t1 = tuple(c / l for c in t1)
    t2 = (n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0])
    return t1, t2


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dataset", type=Path)
    ap.add_argument("--out", type=Path, help="output .ply (default <dataset>/proxy.ply)")
    ap.add_argument("--stride", type=int, default=3, help="use every N-th pixel in x and y")
    ap.add_argument("--opacity", type=float, default=0.9)
    ap.add_argument("--overlap", type=float, default=0.6,
                    help="disc standard deviation in units of the sample spacing")
    args = ap.parse_args()

    d = args.dataset
    transforms = json.loads((d / "transforms_train.json").read_text())
    fl = transforms["fl_x"]
    logit = math.log(args.opacity / (1 - args.opacity))
    records = []
    for frame in transforms["frames"]:
        stem = frame["file_path"].split("/")[-1]
        m = frame["transform_matrix"]
        eye = (m[0][3], m[1][3], m[2][3])
        right = (m[0][0], m[1][0], m[2][0])
        upv = (m[0][1], m[1][1], m[2][1])
        fwd = (-m[0][2], -m[1][2], -m[2][2])
        w, h, depth = read_pfm(d / "geometry" / "train" / f"{stem}_depth.pfm")
        _, _, normal = read_pfm(d / "geometry" / "train" / f"{stem}_normal.pfm")
        _, _, rgb = read_pfm(d / "train" / f"{stem}.pfm")
        for y in range(args.stride // 2, h, args.stride):
            for x in range(args.stride // 2, w, args.stride):
                z = depth[y][x][0]
                if z <= 0:
                    continue
                # Pixel centre ray in the camera frame (+y up in the image).
                u = (x + 0.5 - w / 2) / transforms["fl_x"]
                v = (h / 2 - (y + 0.5)) / transforms["fl_y"]
                ray = tuple(fwd[k] + u * right[k] + v * upv[k] for k in range(3))
                p = tuple(eye[k] + z * ray[k] for k in range(3))
                n = normal[y][x]
                nl = math.sqrt(sum(c * c for c in n))
                if nl == 0:
                    continue
                n = tuple(c / nl for c in n)
                # Footprint of one sample on the surface: stretched by 1 / cos
                # along the view direction's projection, as a pixel footprint is.
                cos = abs(sum(n[k] * ray[k] for k in range(3))) / math.sqrt(sum(c * c for c in ray))
                sigma = args.overlap * args.stride * z / fl
                t1, t2 = tangent_frame(n, ray)
                q = quat_from_frame(t1, t2, n)
                dc = tuple((c - 0.5) / C0 for c in rgb[y][x])
                scales = (math.log(sigma / max(cos, 0.2)), math.log(sigma), math.log(0.05 * sigma))
                records.append(struct.pack("<17f", *p, *n, *dc, logit, *scales, *q))
    out = args.out or d / "proxy.ply"
    with open(out, "wb") as f:
        f.write(("ply\nformat binary_little_endian 1.0\n"
                 f"element vertex {len(records)}\n" +
                 "".join(f"property float {p}\n" for p in
                         ("x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
                          "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3")) +
                 "end_header\n").encode())
        f.writelines(records)
    print(f"Wrote {out}: {len(records)} Gaussians from {len(transforms['frames'])} views")


if __name__ == "__main__":
    sys.exit(main())
