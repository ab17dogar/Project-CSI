#!/usr/bin/env python3
"""Generate the benchmark scene suite (benchmarks/scenes/*.xml).

Each scene isolates a light-transport situation a method is usually judged on:

  cornell_box     diffuse interreflection, small area light (NEE-friendly)
  cornell_glossy  glossy floor, rough metal and glass spheres (MIS-friendly,
                  plus caustics)
  veach_mis       Veach's four plates x four lights test: the canonical case
                  where neither BSDF nor light sampling alone works everywhere
  glass_caustic   a glass sphere focusing a small light (hard for any
                  unidirectional path tracer)
  env_objects     objects under an HDR sky with a small, bright sun
                  (importance-sampled environment lighting)
  sun_sky_bunny   a 144k-triangle mesh under the analytic sun and sky
                  (delta light + procedural environment, BVH-heavy)
  gaussian_lights a Gaussian field as the only light: 400 small bright
                  Gaussians spread over the scene and a dim wall of large
                  ones (many lights at very different distances: light
                  selection by power versus by a light hierarchy)

Run from the repository root:  python3 benchmarks/make_scenes.py
Pure standard library; the output is deterministic.
"""
import math
import os
import random
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "scenes")


def v3(tag, x, y, z):
    return f'<{tag} x="{x:.6g}" y="{y:.6g}" z="{z:.6g}" />'


def rgb(tag, r, g, b):
    return f'<{tag} r="{r:.6g}" g="{g:.6g}" b="{b:.6g}" />'


class Scene:
    def __init__(self, width, aspect, max_depth, look_from, look_at, fov,
                 sun_dir=(0, 1, 0), sun_intensity=0.0, sun_color=(1, 1, 1),
                 hdri=None):
        self.head = [
            "<Config>",
            f'  <Width value="{width}" />',
            f'  <Aspect_ratio value="{aspect:.6g}" />',
            '  <Samples_Per_Pixel value="64" />',
            f'  <Max_Depth value="{max_depth}" />',
            "</Config>",
            "<Camera>",
            "  " + v3("Look_From", *look_from),
            "  " + v3("Look_at", *look_at),
            "  " + v3("Up", 0, 1, 0),
            f'  <FOV angle="{fov:.6g}" />',
            "</Camera>",
            "<Lights>",
            "  <Sun>",
            "    " + v3("Direction", *sun_dir),
            f'    <Intensity value="{sun_intensity:.6g}" />',
            "    " + rgb("Color", *sun_color),
            "  </Sun>",
            "</Lights>",
        ]
        self.materials = []
        self.objects = []
        self.hdri = hdri

    def material(self, kind, name, color=None, **params):
        lines = [f'  <{kind} name="{name}">']
        if color is not None:
            lines.append("    " + rgb("Color", *color))
        for key, value in params.items():
            if isinstance(value, tuple):
                lines.append("    " + rgb(key, *value))
            else:
                lines.append(f'    <{key} value="{value:.6g}" />')
        lines.append(f"  </{kind}>")
        self.materials += lines

    def quad(self, q, u, v, mat):
        self.objects += [
            "  <Quad>",
            "    " + v3("Position", *q),
            "    " + v3("U", *u),
            "    " + v3("V", *v),
            f'    <Material name="{mat}" />',
            "  </Quad>",
        ]

    def sphere(self, center, radius, mat):
        self.objects += [
            "  <Sphere>",
            f'    <Radius value="{radius:.6g}" />',
            "    " + v3("Position", *center),
            "    " + v3("Scale", 1, 1, 1),
            "    " + v3("Rotation", 0, 0, 0),
            f'    <Material name="{mat}" />',
            "  </Sphere>",
        ]

    def box(self, center, size, angle_deg, mat):
        """Axis-aligned box rotated about +y; five visible faces (no bottom)."""
        cx, y0, cz = center
        sx, sy, sz = size
        a = math.radians(angle_deg)
        ex = (math.cos(a) * sx, 0.0, -math.sin(a) * sx)  # local x edge
        ez = (math.sin(a) * sz, 0.0, math.cos(a) * sz)   # local z edge
        ey = (0.0, sy, 0.0)
        corner = (cx - 0.5 * (ex[0] + ez[0]), y0, cz - 0.5 * (ex[2] + ez[2]))

        def add(p, q):
            return tuple(pi + qi for pi, qi in zip(p, q))

        self.quad(add(corner, ey), ex, ez, mat)                 # top
        self.quad(corner, ey, ex, mat)                           # front
        self.quad(add(corner, ez), ex, ey, mat)                  # back
        self.quad(corner, ez, ey, mat)                           # left
        self.quad(add(corner, ex), ey, ez, mat)                  # right

    def mesh(self, file, position, scale, mat):
        self.objects += [
            "  <Mesh>",
            "    " + v3("Position", *position),
            "    " + v3("Scale", scale, scale, scale),
            "    " + v3("Rotation", 0, 0, 0),
            f'    <Material name="{mat}" />',
            f'    <File name="{file}" />',
            "  </Mesh>",
        ]

    def field(self, file, sampler=None):
        extra = f' sampler="{sampler}"' if sampler else ""
        self.objects += [f'  <GaussianField file="{file}"{extra} />']

    def write(self, name, description):
        lines = ['<?xml version="1.0"?>',
                 f"<!-- {description} Generated by benchmarks/make_scenes.py. -->",
                 "<ItemContainer>"]
        lines += ["  " + l for l in self.head]
        lines += ["  <Materials>"] + ["  " + l for l in self.materials] + ["  </Materials>"]
        lines += ["  <Objects>"] + ["  " + l for l in self.objects] + ["  </Objects>"]
        if self.hdri:
            lines.append(f'  <Environment hdri="{self.hdri}" intensity="1.0" />')
        lines.append("</ItemContainer>")
        with open(os.path.join(OUT, name + ".xml"), "w", newline="\n") as f:
            f.write("\n".join(lines) + "\n")


C0 = 0.28209479177387814


def write_gaussian_ply(name, gaussians):
    """3DGS-layout binary PLY (SH degree 0). gaussians: (mean, scale, radiance,
    opacity), axis-aligned. Deterministic bytes, so the CI check can diff it."""
    props = ("x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2", "opacity",
             "scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3")
    header = ("ply\nformat binary_little_endian 1.0\n"
              f"element vertex {len(gaussians)}\n" +
              "".join(f"property float {p}\n" for p in props) + "end_header\n")
    with open(os.path.join(OUT, name), "wb") as f:
        f.write(header.encode())
        for mean, scale, radiance, opacity in gaussians:
            dc = [(c - 0.5) / C0 for c in radiance]
            f.write(struct.pack("<17f", *mean, 0, 0, 0, *dc, math.log(opacity / (1 - opacity)),
                                *(math.log(s) for s in scale), 1, 0, 0, 0))


def cornell_walls(s, floor_mat="white"):
    s.material("Lambertian", "white", (0.73, 0.73, 0.73))
    s.material("Lambertian", "red", (0.63, 0.065, 0.05))
    s.material("Lambertian", "green", (0.14, 0.45, 0.091))
    # Box [-1,1] x [0,2] x [-1,1], open towards +z.
    s.quad((-1, 0, -1), (0, 0, 2), (2, 0, 0), floor_mat)      # floor
    s.quad((-1, 2, -1), (2, 0, 0), (0, 0, 2), "white")        # ceiling
    s.quad((-1, 0, -1), (2, 0, 0), (0, 2, 0), "white")        # back
    s.quad((-1, 0, -1), (0, 2, 0), (0, 0, 2), "red")          # left
    s.quad((1, 0, -1), (0, 0, 2), (0, 2, 0), "green")         # right
    # Ceiling light ("light" material defined by the caller): same footprint
    # ratio as the original Cornell box. It must emit downward only: a
    # two-sided light 1 mm under the ceiling lights the gap above it with an
    # almost singular geometry term, which gave pure light sampling rare
    # fireflies that carried 99% of its error.
    s.quad((-0.235, 1.999, -0.19), (0.47, 0, 0), (0, 0, 0.38), "light")


def light_material(s, name, radiance, two_sided=True):
    if two_sided:
        s.material("Emissive", name, radiance, Strength=1.0)
    else:
        s.material("Emissive", name, radiance, Strength=1.0, TwoSided=0)


def main():
    os.makedirs(OUT, exist_ok=True)

    # ------------------------------------------------------------------
    s = Scene(256, 1.0, 12, (0, 1, 3.9), (0, 1, 0), 39.3)
    cornell_walls(s)
    light_material(s, "light", (17.0, 12.0, 4.0), two_sided=False)
    s.box((0.33, 0, 0.37), (0.6, 0.6, 0.6), -18, "white")
    s.box((-0.34, 0, -0.29), (0.6, 1.2, 0.6), 17, "white")
    s.write("cornell_box", "Diffuse Cornell box with a small ceiling light.")

    # ------------------------------------------------------------------
    s = Scene(256, 1.0, 12, (0, 1, 3.9), (0, 1, 0), 39.3)
    s.material("PBR", "glossy_floor", (0.73, 0.73, 0.73), Metallic=0.0, Roughness=0.2)
    cornell_walls(s, floor_mat="glossy_floor")
    light_material(s, "light", (17.0, 12.0, 4.0), two_sided=False)
    s.material("PBR", "copper", (0.95, 0.64, 0.54), Metallic=1.0, Roughness=0.15)
    s.material("Dielectric", "glass", None, IOR=1.5)
    s.sphere((-0.4, 0.4, -0.3), 0.4, "copper")
    s.sphere((0.45, 0.35, 0.3), 0.35, "glass")
    s.write("cornell_glossy", "Cornell box with a glossy floor, rough copper and glass spheres.")

    # ------------------------------------------------------------------
    # Veach's MIS scene (Veach 1997, fig. 9.2): plates get rougher towards
    # the camera, lights get bigger left to right; all lights emit the same
    # power, so radiance scales with 1 / r^2.
    cam = (0.0, 2.0, 12.0)
    s = Scene(384, 1.5, 4, cam, (0, 0.6, 0), 30)
    s.material("Lambertian", "backdrop", (0.25, 0.25, 0.25))
    s.quad((-12, -1.5, -6), (24, 0, 0), (0, 0, 14), "backdrop")      # floor
    s.quad((-12, -1.5, -6), (0, 12, 0), (24, 0, 0), "backdrop")      # back wall
    light_center = (0.0, 4.0, -3.0)
    radii = [0.03, 0.1, 0.3, 0.9]
    xs = [-3.75, -1.25, 1.25, 3.75]
    for i, (x, r) in enumerate(zip(xs, radii)):
        L = 0.9 / (r * r)  # equal power: L * r^2 constant
        light_material(s, f"light{i}", (L, L, L))
        s.sphere((x, light_center[1], light_center[2]), r, f"light{i}")
    roughness = [0.03, 0.08, 0.18, 0.35]
    plates = [(0.0, -0.3, 3.0), (0.0, 0.25, 1.9), (0.0, 0.75, 0.75), (0.0, 1.15, -0.45)]
    for i, (c, rough) in enumerate(zip(plates, roughness[::-1])):
        s.material("PBR", f"plate{i}", (0.8, 0.8, 0.8), Metallic=1.0, Roughness=rough)
        to_cam = [cam[k] - c[k] for k in range(3)]
        to_light = [light_center[k] - c[k] for k in range(3)]
        n1 = math.sqrt(sum(t * t for t in to_cam))
        n2 = math.sqrt(sum(t * t for t in to_light))
        h = [to_cam[k] / n1 + to_light[k] / n2 for k in range(3)]
        hn = math.sqrt(sum(t * t for t in h))
        n = [t / hn for t in h]
        # Plate spans x and the direction perpendicular to n within the yz plane.
        t = (0.0, n[2], -n[1])
        depth = 0.9
        u = (8.0, 0.0, 0.0)
        vv = (t[0] * depth, t[1] * depth, t[2] * depth)
        q = (c[0] - 4.0, c[1] - 0.5 * vv[1], c[2] - 0.5 * vv[2])
        s.quad(q, u, vv, f"plate{i}")
    s.write("veach_mis", "Veach's MIS test: four glossy plates, four sphere lights of equal power.")

    # ------------------------------------------------------------------
    s = Scene(256, 1.0, 12, (0, 1.6, 4.2), (0, 0.6, 0), 40)
    s.material("Lambertian", "floor", (0.8, 0.8, 0.8))
    s.material("Lambertian", "wall", (0.6, 0.6, 0.65))
    s.material("Dielectric", "glass", None, IOR=1.5)
    s.material("Lambertian", "diffuse", (0.75, 0.25, 0.2))
    light_material(s, "light", (400.0, 380.0, 340.0))
    s.quad((-6, 0, -6), (0, 0, 12), (12, 0, 0), "floor")
    s.quad((-6, 0, -2.5), (12, 0, 0), (0, 8, 0), "wall")
    s.sphere((0.0, 0.7, 0.0), 0.7, "glass")
    s.sphere((-1.6, 0.4, -0.6), 0.4, "diffuse")
    s.sphere((1.6, 3.2, 0.8), 0.12, "light")
    s.write("glass_caustic", "A glass sphere focusing a small spherical light onto a diffuse floor.")

    # ------------------------------------------------------------------
    s = Scene(320, 4 / 3, 10, (0, 1.3, 5.2), (0, 0.65, 0), 38, hdri="../env/sunsky.hdr")
    s.material("Lambertian", "ground", (0.5, 0.5, 0.5))
    s.material("Lambertian", "clay", (0.8, 0.3, 0.2))
    s.material("PBR", "plastic", (0.15, 0.35, 0.8), Metallic=0.0, Roughness=0.3)
    s.material("PBR", "gold", (1.0, 0.78, 0.34), Metallic=1.0, Roughness=0.25)
    s.material("Dielectric", "glass", None, IOR=1.5)
    s.quad((-20, 0, -20), (0, 0, 40), (40, 0, 0), "ground")
    s.sphere((-1.8, 0.5, 0.2), 0.5, "clay")
    s.sphere((-0.6, 0.5, -0.4), 0.5, "plastic")
    s.sphere((0.6, 0.5, -0.4), 0.5, "gold")
    s.sphere((1.8, 0.5, 0.2), 0.5, "glass")
    s.write("env_objects", "Spheres of four materials under an HDR sky with a small, bright sun.")

    # ------------------------------------------------------------------
    s = Scene(320, 4 / 3, 8, (0.2, 1.6, 3.6), (-0.15, 0.7, 0.3), 40,
              sun_dir=(0.6, 0.8, 0.45), sun_intensity=2.0, sun_color=(1.0, 0.95, 0.85))
    s.material("Lambertian", "ground", (0.45, 0.45, 0.45))
    s.material("PBR", "bunny", (0.85, 0.85, 0.82), Metallic=0.0, Roughness=0.45)
    s.quad((-20, 0, -20), (0, 0, 40), (40, 0, 0), "ground")
    s.mesh("bunny.obj", (0.0, -0.02, 0.0), 1.0, "bunny")
    s.write("sun_sky_bunny", "Stanford bunny (144k triangles) under the analytic sun and sky.")

    # ------------------------------------------------------------------
    # Many Gaussian lights at very different distances. The field is the
    # only light; geometry receives it.
    rng = random.Random(7)
    lights = []
    for i in range(20):
        for j in range(20):
            x = -7 + 14 * (i + 0.5) / 20 + rng.uniform(-0.2, 0.2)
            z = -7 + 14 * (j + 0.5) / 20 + rng.uniform(-0.2, 0.2)
            warm = rng.uniform(0.0, 1.0)
            radiance = (60.0, 45.0 + 15.0 * warm, 25.0 + 30.0 * (1 - warm))
            lights.append(((x, rng.uniform(2.2, 2.8), z), (0.04, 0.04, 0.04), radiance, 0.95))
    for i in range(20):
        for j in range(10):
            x = -7 + 14 * (i + 0.5) / 20
            y = 3.0 * (j + 0.5) / 10
            lights.append(((x, y, -7.5), (0.45, 0.3, 0.05), (0.25, 0.3, 0.4), 0.9))
    write_gaussian_ply("gaussian_lights.ply", lights)
    s = Scene(320, 4 / 3, 6, (0.0, 2.0, 6.0), (0.0, 0.6, 0.0), 45)
    s.material("Lambertian", "floor", (0.5, 0.5, 0.5))
    s.material("PBR", "blue", (0.08, 0.22, 0.85), Metallic=0.0, Roughness=0.3)
    s.material("Lambertian", "white", (0.73, 0.73, 0.73))
    s.quad((-8, 0, -8), (0, 0, 16), (16, 0, 0), "floor")
    s.sphere((0.0, 0.6, 0.0), 0.6, "blue")
    s.box((1.7, 0, -0.8), (0.8, 1.2, 0.8), 25, "white")
    s.field("gaussian_lights.ply")
    s.write("gaussian_lights", "A Gaussian field of 400 small bright lights and a dim wall lighting a floor, a sphere and a box.")


if __name__ == "__main__":
    main()
