#!/usr/bin/env python3
"""Check that a make_dataset.py dataset's poses agree with its geometry.

    python3 benchmarks/check_dataset.py datasets/cornell_box

Reads transforms_train.json the way the reference 3DGS loader does (flip the
camera's y and z axes from the Blender to the COLMAP convention, invert to
world-to-camera, focal length from camera_angle_x), projects every point of
points3d.ply into every training view, and compares its depth with the view's
rendered depth map. A point may be hidden behind the visible surface
(occlusion), but none may float in front of it. Depth varies within a pixel
at grazing angles, so a point counts as in front only if it is nearer than
all 3x3 neighbouring pixel centers. Edges thinner than a pixel still let a
few points through, a fraction that falls as 1 / width: on cornell_box about
3-6% at 32 px, under 1% at 128 px, 0.1-0.3% at 256 px. Exits 1 if more than
--tolerance of the points in any view are in front, or a view sees none.

Mutation-tested: without the axis flip no point is in front of any camera;
with the image rows flipped, 24-54% float in front; with the vertical field
of view used as the horizontal one, 26-50% (non-square images only; on
square ones the two are equal). Standard library only.
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

from make_dataset import read_pfm


def read_points(path):
    raw = path.read_bytes()
    end = raw.index(b"end_header\n") + len(b"end_header\n")
    header = raw[:end].decode()
    n = int(next(l for l in header.splitlines() if l.startswith("element vertex")).split()[-1])
    size = struct.calcsize("<6f3B")
    return [struct.unpack_from("<3f", raw, end + i * size) for i in range(n)]


def invert_rigid(m):
    """Inverse of a 4x4 rotation + translation, as 3x3 R and 3-vector t."""
    r = [[m[j][i] for j in range(3)] for i in range(3)]  # transpose
    t = [-sum(r[i][k] * m[k][3] for k in range(3)) for i in range(3)]
    return r, t


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dataset", type=Path)
    ap.add_argument("--tolerance", type=float, default=0.02,
                    help="largest allowed fraction of points in front of the depth map")
    args = ap.parse_args()

    points = read_points(args.dataset / "points3d.ply")
    transforms = json.loads((args.dataset / "transforms_train.json").read_text())
    fovx = transforms["camera_angle_x"]
    failed = False
    for frame in transforms["frames"]:
        c2w = [row[:] for row in frame["transform_matrix"]]
        for row in c2w[:3]:  # Blender/OpenGL -> COLMAP: negate the y and z axes
            row[1], row[2] = -row[1], -row[2]
        r, t = invert_rigid(c2w)
        stem = frame["file_path"].split("/")[-1]
        w, h, depth = read_pfm(args.dataset / "geometry" / "train" / f"{stem}_depth.pfm")
        f = w / (2 * math.tan(fovx / 2))  # 3DGS derives the y focal length the same way
        seen = in_front = 0
        for p in points:
            x, y, z = (sum(r[i][k] * p[k] for k in range(3)) + t[i] for i in range(3))
            if z <= 0:
                continue
            u, v = f * x / z + w / 2, f * y / z + h / 2
            if not (0 <= u < w and 0 <= v < h):
                continue
            seen += 1
            ui, vi = int(u), int(v)
            neighbours = [depth[j][i][0] for j in range(max(0, vi - 1), min(h, vi + 2))
                          for i in range(max(0, ui - 1), min(w, ui + 2))]
            neighbours = [d for d in neighbours if d > 0]
            if neighbours and z < min(neighbours) * 0.999:
                in_front += 1
        frac = in_front / seen if seen else 0.0
        print(f"{stem}: {seen} points in view, {frac * 100:.2f}% in front of the depth map")
        failed |= seen == 0 or frac > args.tolerance
    if failed:
        sys.exit("pose / geometry mismatch")
    print("poses consistent with geometry")


if __name__ == "__main__":
    main()
