#!/usr/bin/env python3
"""Write benchmarks/env/sunsky.hdr: a synthetic equirectangular sky.

A blue-to-white sky gradient, a darker ground, and a small (1 degree radius)
sun carrying roughly three quarters of the irradiance on a horizontal
surface. Concentrating most energy in a few texels is what makes
environment importance sampling matter.

Mapping matches the renderer (src/engine/light.cpp): v = 0 is +y (zenith),
phi = 2*pi*u - pi, direction = (sin t cos p, cos t, sin t sin p).

Pure standard library; deterministic. Run from the repository root:
    python3 benchmarks/make_sunsky_hdr.py
"""
import math
import os
import struct

W, H = 512, 256
SUPERSAMPLE = 4
SUN_DIR = (0.55, 0.62, 0.56)       # normalized below
SUN_RADIUS_DEG = 1.0
SUN_RADIANCE = (4600.0, 4300.0, 3800.0)
ZENITH = (0.18, 0.34, 0.75)
HORIZON = (0.75, 0.82, 0.95)
GROUND = (0.12, 0.10, 0.08)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "env", "sunsky.hdr")


def normalize(v):
    n = math.sqrt(sum(c * c for c in v))
    return tuple(c / n for c in v)


SUN = normalize(SUN_DIR)
COS_SUN = math.cos(math.radians(SUN_RADIUS_DEG))


def radiance(d):
    y = d[1]
    if y >= 0.0:
        t = y ** 0.5
        sky = tuple((1 - t) * h + t * z for h, z in zip(HORIZON, ZENITH))
    else:
        t = min(1.0, -y * 4.0)
        sky = tuple((1 - t) * h * 0.6 + t * g for h, g in zip(HORIZON, GROUND))
    if sum(a * b for a, b in zip(d, SUN)) >= COS_SUN:
        return SUN_RADIANCE
    return sky


def texel(i, j):
    acc = [0.0, 0.0, 0.0]
    for a in range(SUPERSAMPLE):
        for b in range(SUPERSAMPLE):
            u = (i + (a + 0.5) / SUPERSAMPLE) / W
            v = (j + (b + 0.5) / SUPERSAMPLE) / H
            theta, phi = v * math.pi, u * 2.0 * math.pi - math.pi
            st = math.sin(theta)
            L = radiance((st * math.cos(phi), math.cos(theta), st * math.sin(phi)))
            for k in range(3):
                acc[k] += L[k]
    n = SUPERSAMPLE * SUPERSAMPLE
    return [c / n for c in acc]


def to_rgbe(rgb):
    m = max(rgb)
    if m < 1e-32:
        return (0, 0, 0, 0)
    mant, exp = math.frexp(m)
    scale = mant * 256.0 / m
    return tuple(min(255, int(c * scale)) for c in rgb) + (exp + 128,)


def rle_channel(values):
    """New-style Radiance RLE for one channel of one scanline."""
    out = bytearray()
    i, n = 0, len(values)
    while i < n:
        run = 1
        while i + run < n and run < 127 and values[i + run] == values[i]:
            run += 1
        if run >= 4:
            out += bytes((128 + run, values[i]))
            i += run
            continue
        start = i
        while i < n and i - start < 128:
            if i + 3 < n and values[i] == values[i + 1] == values[i + 2] == values[i + 3]:
                break
            i += 1
        out += bytes((i - start,)) + bytes(values[start:i])
    return out


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
        f.write(f"-Y {H} +X {W}\n".encode())
        for j in range(H):
            row = [to_rgbe(texel(i, j)) for i in range(W)]
            f.write(struct.pack("BBBB", 2, 2, W >> 8, W & 255))
            for c in range(4):
                f.write(rle_channel([px[c] for px in row]))
    # Irradiance split on a horizontal surface, for the record.
    sun_e = SUN_RADIANCE[1] * 2 * math.pi * (1 - COS_SUN) * SUN[1]
    print(f"wrote {OUT} ({W}x{H}); sun irradiance on the ground ~{sun_e:.2f}")


if __name__ == "__main__":
    main()
