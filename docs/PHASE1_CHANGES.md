# Phase 1: correctness overhaul

Goal: a renderer whose numbers a reviewer can trust. Every change below fixes
a correctness problem in the original engine or is needed for the fixes. The
test suite (`tests/`, run by `ctest`) checks each one.

## Bugs found in the original engine

| Area | Problem | Effect |
|---|---|---|
| Material interface | `scatter()` returned only a sampling weight. No BSDF evaluation or pdf | NEE and MIS were impossible to implement correctly |
| Direct lighting | Point-light NEE added `lightColor * cos * I/d^2` with no BSDF factor, after the recursive term | Surfaces received light independent of their albedo (a red wall lit white) |
| Sun | Indirect radiance was multiplied by `0.3` when the sun was occluded, by `sunColor` otherwise | Not an estimator of anything; global illumination scaled by sun visibility |
| MIS | Weights used `pdf_light = 1.0` (wrong units) against a cosine pdf | Arbitrary weighting; delta lights cannot be MIS-weighted at all |
| Dead code | `pdf.h`, `mis.h`, `scatter_record.h`, `mixture_pdf`, `hittable_pdf` were never used | The README's MIS/NEE claims described code that did not run |
| GGX | Sampled the plain NDF despite the "VNDF" name, never divided by the pdf, clamped to 10 | Biased, and inconsistent with any light sampling |
| PBR / SSS | Ad-hoc perturbations with no pdf; SSS leaked energy | Could not be combined with light sampling |
| Dielectric | Schlick approximation; tint applied to reflection too; loader forced a 0.95-0.97 tint | Clear glass lost 3-5% of light per crossing |
| BVH | Median split (README claimed SAH); copied the full object list at every node | O(n²) build: a 144k-triangle mesh did not finish in 5 minutes |
| Instances | `translate` / `rotate_y` re-ran `set_face_normal` on an already-flipped normal | `front_face` was always true, so glass inside instances refracted wrongly |
| Triangles | Parallel test used an absolute `1e-5` threshold | Small triangles of dense meshes were silently missed |
| Camera | Pixel coordinate divided by `width - 1` | Image stretched by one pixel |
| Textures / HDRI | 8-bit textures used as linear; HDRI loader could not read `.hdr` | Wrong albedos; no real image-based lighting |
| Output | `sqrt()` gamma, 8-bit only | Wrong tone curve, no float output for error metrics |
| RNG | `std::random_device`-seeded per thread | Renders were not reproducible |
| Build | `glfw`/`imgui` gitlinks without `.gitmodules` | Fresh clones could not configure; CI could not pass |

## What replaced it

- **`material`**: `sample(wo) / eval(wo, wi) / pdf(wo, wi)` (`src/engine/material.h`).
  Lambertian, textured Lambertian, metallic-roughness PBR (GGX,
  height-correlated Smith G2, VNDF sampling, Kelemen-coupled diffuse with a
  tabulated specular albedo), conductors, smooth dielectrics (exact Fresnel,
  1/η² radiance scaling), emitters, isotropic phase function, and diffuse
  translucency (an honest replacement for the old "SSS").
- **Lights** (`light.h`): triangle, parallelogram and sphere area lights (cone
  sampling from outside), point, directional, importance-sampled HDR
  environment, procedural sky. `light_sampler.h` provides power-proportional
  selection behind a spatially aware interface.
- **Integrator** (`integrator.cpp`): iterative path tracer, NEE plus power
  heuristic MIS, Russian roulette with η-scale correction, and a strategy
  switch (`--integrator mis|bsdf|nee`) for ablations.
- **BVH** (`bvh.h/.cpp`): binned SAH, flattened, iterative front-to-back
  traversal, early-exit occlusion queries; per-mesh BVH over triangles stored
  by value.
- **Reproducibility**: PCG32 re-seeded per (pixel, sample, seed).
- **Output**: sRGB PNG and linear `.pfm` (`--pfm`).

## Verification (25 tests, ~10M checks)

- χ² goodness-of-fit of every BSDF sampler against its own pdf (and a
  deliberately broken sampler, to show the test catches errors).
- sample/eval/pdf agreement, reciprocity, and directional albedo ≤ 1 for
  every material; Snell's law and Fresnel probabilities for glass.
- `sample_li` / `pdf_li` agreement, normalization and solid angle for every
  light shape and the environment map.
- Analytic irradiance (rectangle form factor, triangles, sphere, point, sun,
  environment) under **each** strategy: MIS, BSDF-only and NEE-only must all
  converge to the closed form.
- White furnace with interreflections (open box + sphere, 256 bounces).
- Strategy agreement on multi-bounce glossy indirect lighting.
- BVH equals brute force on 30k rays (mixed primitives, degenerate centroids,
  meshes).
- Bit-identical images across thread counts.
- Clean under AddressSanitizer + UndefinedBehaviorSanitizer, GCC and Clang.

Bugs the suite caught during the rewrite:

- **Sphere-light self-shadowing.** Shadow rays were spawned parallel to the
  sampled direction from an offset origin, and clipped the emitter near its
  silhouette. About 5% of samples were wrongly occluded. The fix aims the shadow
  ray at the sampled point (`shadow_ray()` in `integrator.cpp`).
- **Test harness early-exit.** A guard compared against the global failure
  count, so one failing test made later tests skip silently.

A mutation check confirms the suite's power: halving the light pdf in the
BSDF-side MIS weight makes the analytic tests fail.

## Deliberately deferred

- **float / SIMD `vec3`.** Profiling (gprof, Cornell scene) puts ~60% of the
  time in BVH traversal and triangle tests and ~20% in shading. Converting
  scalar `vec3` to float would not move that. The lever is a wide SIMD BVH or
  Embree, and it belongs with the Gaussian traversal work. Double precision
  is also the safer default for a reference renderer accumulating
  million-sample images.
- **Smooth vertex normals** for OBJ/glTF meshes: faceting is visible on
  the bunny.
- **True subsurface scattering** (volumetric random walk) needs interior-medium
  tracking in the integrator.
- **OIDN auxiliary buffers** (albedo/normal). OIDN was not installed in the
  build environment, so the denoiser path is untested here.
