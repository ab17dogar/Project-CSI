# Research direction: physically based insertion of synthetic objects into captured Gaussian scenes

Status: proposal, revised 18 October 2026 after a second literature check
([LITERATURE_REVIEW.md](LITERATURE_REVIEW.md)). The check worked from
abstracts and documentation, not full papers. Its "verify" list must be
cleared before Phase 3 code is committed to.

## Starting point

Render a captured, photorealistic scene with synthetic objects inserted, and
reconstruct the scene well enough that the insertion is physically
consistent. The literature check shows the generic form is taken:
LightHarmony3D (2026) inserts meshes into 3DGS scenes and evaluates on
path-traced ground-truth pairs (LH3D-Bench); DOT3D (EGSR 2026) and D3DR
harmonize with diffusion models. What remains open is doing it *physically*,
in both directions, with the capture itself as the light, and measuring where
the error comes from. That splits into four contributions.

## Contributions

### C1. The capture lights the object

A 3D Gaussian Splatting (3DGS) capture acts as a **volumetric,
view-dependent, self-occluding many-light emitter** in an unbiased path
tracer: next event estimation (NEE) toward the Gaussians plus MIS with BSDF
rays that pass through them.

- *Prior art and difference.* LightHarmony3D lights the object with one
  environment map per insertion point (no parallax, no near field); GaSLight
  turns bright Gaussians into Cycles surface emitters (no interreflection, no
  volumetric transmittance); 3DGRT finds the capture only by BSDF sampling;
  the Gaussian-surfels paper samples surfels (surfaces). PTIR-GS, EAG-PT and
  Condor et al. are the **verify** items: if any light-samples captured
  radiance, C1 narrows to its technical core below.
- *Technical core.* (a) A light hierarchy over Gaussians whose nodes bound
  spherical-harmonic emission over a cone of directions; the nearest tool is
  the spherical-Gaussian light tree of Tokuyoshi et al. 2024. (b) MIS
  between a BSDF ray's line integral of emission through the field and an
  NEE point sample, in a common measure (starting from Simon et al. 2017).
  (c) Closed-form optical depth of a Gaussian along a ray (erf) for shadow
  rays through the field.
- *Design for (b) in the stage 1 model.* The renderer uses the 3DGRT
  particle model (each Gaussian evaluated at its maximum response along a
  ray, composited front to back; [GAUSSIAN_FIELDS.md](GAUSSIAN_FIELDS.md)).
  There the radiance arriving from the field is a sum of per-Gaussian terms
  T_i alpha_i c_i, so MIS can be done per Gaussian, as for many separate
  area lights: NEE picks Gaussian i with probability P_i, samples a point
  from its 3D distribution and connects to it. The induced direction pdf,
  p_i(w) = integral over t > 0 of N(x + t w; mu_i, Sigma_i) t^2 dt, is a
  closed-form line integral (erf), so a BSDF ray can weight every Gaussian
  it passes with P_i p_i(w). Visibility is the product of (1 - alpha_j) of
  the Gaussians in front, which shadow rays already compute.
- *Model statement.* 3DGS is trained with alpha compositing, not volume
  rendering. The estimator is unbiased for a stated volumetric model (EVER-
  style or Condor-style kernels); the gap to the trained model is measured,
  not hidden (Celarek et al. 2025).

### C2. The object changes the capture

The object casts shadows on the captured scene, blocks captured light, and
bounces its own color onto captured surfaces. This is **differential
rendering inside the field** (Debevec 1998), without a proxy mesh:

    final = L_capture + (L_with − L_without)        on captured pixels
    final = L_with                                  on object pixels

where L_with and L_without are path traced in the reconstructed local scene
with and without the object.

- *Common random numbers for free.* This renderer seeds every sample from
  (pixel, sample index, seed), so rendering both terms with the same seed
  correlates them exactly: a path that never meets the object contributes the
  same value to both and cancels. Only paths that interact with the object
  add variance. First measurement, on ground-truth renders of `cornell_box`
  with an inserted sphere (one view, 256 spp): the difference's mean squared
  noise outside the sphere is 214x lower than with independent seeds
  ([DATASETS.md](DATASETS.md)). Repeating this inside a reconstructed field,
  and analysing it, is part of the contribution.
- *Difference from prior art.* LightHarmony3D's shadow ratio uses a proxy
  mesh and a distant environment map; Omniverse uses a matte proxy mesh;
  3DGRT dims Gaussians heuristically. None of the summaries mention
  object-to-capture interreflection.
- *Open problem.* A volumetric field has no surface. Receivers need a
  position and normal: the median termination depth along the camera ray
  with the normal of the dominant Gaussian's shortest axis is the obvious
  start; the choice must be validated against ground truth (C4).

### C3. Reconstruct the scene where the insertion touches it

C2 needs the reflectance of captured surfaces near the object. Instead of a
global inverse rendering (crowded: R3DG, IRGS, PTIR-GS), estimate it
**locally and forward**: for a diffuse receiver, ρ ≈ π L_capture / E, where E
is the irradiance the path tracer computes from the field itself (C1's
machinery), only for receivers inside the object's influence region.

- For diffuse receivers C2 + C3 reduce to a ratio,
  final = L_capture · E_with / E_without, where E_with includes the light the
  object reflects. A shadow ratio *with* color bleeding.
- E_without does not depend on the object. Precompute it once per receiver
  with many samples, so the ratio estimator's bias (a ratio of noisy
  estimates is biased) stays small. Report that bias explicitly.
- Glossy receivers: keep the view-dependent SH residual of the capture as is
  and apply the ratio to the diffuse part only; measure the error that
  causes.
- Novelty is modest (it is Debevec's local-scene estimate inside a radiance
  field). Present it as the enabling component, not the headline.

### C4. Closed-loop evaluation that attributes error to its source

The renderer produces both the training data and the ground truth:

1. Path trace a synthetic scene from many views
   (`benchmarks/make_dataset.py`, Stage 0 below).
2. Train 3DGS on those images (any standard trainer; GPU).
3. Insert the object into the reconstruction and render with C1-C3.
4. Compare with the same object path traced in the *original* scene, in HDR.

LH3D-Bench already does steps 1-4 with LDR PSNR / SSIM / LPIPS. The
contribution is in what is measured:

- **Oracle swaps.** Replace one stage at a time with ground truth: true
  lights and geometry instead of the field (lighting error), true reflectance
  instead of C3 (reflectance error), true receiver positions (geometry
  error), reference-level sample counts (Monte Carlo error). The error budget
  then says which stage to improve.
- **Region metrics.** Separate error on the object, in its influence region
  (shadow, bleeding: where ground truth with and without the object differ)
  and elsewhere (must be untouched).
- **HDR and equal time.** relMSE and HDR-FLIP with reference-noise
  correction and convergence curves, from the Phase 2 pipeline.
- **One renderer for ground truth and methods.** Baselines are implemented in
  the same renderer: environment map at the insertion point (LightHarmony3D's
  lighting model), BSDF-only (3DGRT-style), bright Gaussians as surface
  emitters (GaSLight-style). Differences are then the method, not the
  renderer.

### Stretch goals

- **Virtual light probes.** Path trace a virtual chrome or diffuse sphere
  anywhere in a capture: the physically computed counterpart of
  DiffusionLight's painted chrome ball. Nearly free once C1 works, and a
  clear figure.
- **MIS compensation** (Karlík et al. 2019) for the field's light pdf.

## Staged plan

| Stage | Content | Depends on |
|---|---|---|
| 0 | Multi-view dataset export: training views, test views with and without the inserted object, poses in the NeRF / Blender `transforms.json` format, depth and normals, `points3d.ply` initialization | nothing; **done** |
| 1 | Gaussian field in the path tracer: load 3DGS `.ply`, acceleration structure over Gaussians, emission and transmittance along rays (particle model), SH evaluation, BSDF-only baseline; unoptimized Gaussian stand-in built from a dataset, and a per-region insertion demo. Tests against closed forms and brute force | 0; **done** |
| 2 | C1: power-based Gaussian selection first, then the SH-bounded light hierarchy; line-integral MIS | 1 |
| 3 | C2 + C3: correlated differential rendering, receiver model, local reflectance | 2 |
| 4 | C4: oracle swaps, region metrics, baselines; captured scenes (Mip-NeRF 360) for qualitative results | 0-3 |

Venues: EGSR 2027 (deadline usually in April), HPG 2027, CGF or JCGT. C4
alone could be a short paper. Training 3DGS needs a CUDA GPU (university
cluster); the renderer stays CPU.

## What a reviewer will ask for

- Equal-time comparisons against all three baselines above, on synthetic
  scenes with ground truth and on real captures.
- Ablations: C1 without the SH bounds, without line-integral MIS; C2 without
  common random numbers; C3 against the oracle reflectance.
- The error budget from C4 on every scene.
- A statement of which radiance model is rendered without bias, and the
  measured gap to the trained 3DGS.

## Fallbacks

- If the verify items take C1: C2 + C3 + C4 still stand (physically based,
  proxy-free two-way insertion with an attributed error budget).
- If the receiver model (C2 open problem) fails: C1 + C4 with
  environment-map baselines.
- Unrelated fallback with the least overlap found: denoiser-aware adaptive
  sampling (allocate samples by estimated post-denoise error; position
  against Kuznetsov et al. 2018).

## How the engine already supports this

- `light_sampler` takes the shading point and normal. A Gaussian light
  hierarchy plugs in behind the same `sample` / `pmf` interface.
- MIS is written against the generic `light::pdf_li`. The analytic tests in
  `tests/test_integrator.cpp` are the template for validating a new emitter
  type.
- The `LightStrategy` switch (`--integrator mis|bsdf|nee`) is the ablation
  harness.
- Per-sample random streams depend only on (pixel, sample, seed): renders are
  bit-identical across threads and passes, and two renders with the same seed
  share random numbers (C2's common random numbers).
- `constant_medium` already gives unbiased stochastic transmittance for
  shadow rays.
- Phase 2 measurement pipeline ([BENCHMARKING.md](BENCHMARKING.md)):
  equal-time rendering, relMSE and HDR-FLIP with reference noise removed,
  convergence and image-grid figures.
- Stage 0 dataset export ([DATASETS.md](DATASETS.md)).
