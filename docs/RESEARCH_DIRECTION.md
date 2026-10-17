# Research direction: next event estimation from 3D Gaussian radiance fields

Status: proposal, October 2026. Literature check done with targeted web
searches, **not** a systematic review. Before committing to an implementation,
do the review steps in [Due diligence](#due-diligence-before-phase-3).

## One-sentence claim

A captured 3D Gaussian Splatting (3DGS) scene can act as a **volumetric,
view-dependent, self-occluding many-light emitter** inside an unbiased path
tracer. Sampling it with next event estimation plus MIS, instead of relying on
BSDF rays that happen to hit it, should give large equal-time variance
reductions when synthetic objects are lit by captured surroundings.

## Why this and not "ray-traced Gaussian splatting"

Ray tracing Gaussians is solved and shipping:

- 3DGRT (Moenne-Loccoz et al., SIGGRAPH Asia 2024) and 3DGUT (CVPR 2025),
  open-sourced as [3DGRUT](https://github.com/nv-tlabs/3dgrut). Secondary rays,
  reflections and refraction through particle scenes.
- [GRay](https://arxiv.org/pdf/2606.30869) (2026). Ray tracing Gaussians
  near rasterization speed.
- [Stochastic ray tracing of transparent Gaussians](https://arxiv.org/pdf/2504.06598).
- Error analysis of the alpha-blending approximations:
  [Celarek et al., CGF 2025](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70032),
  [Volumetrically Consistent 3DGS, CVPR 2025](https://chinmay0301.github.io/vol3dgs/).
- Production: [Arnold 7.5.2 (June 2026)](https://www.cgchannel.com/2026/06/autodesk-releases-arnold-7-5-2-with-support-for-gaussian-splatting/)
  renders and relights splats; USD 26.03 has a Gaussian splat schema.

A CPU path tracer cannot compete there on speed, and speed is what that
subfield competes on.

## The gap

Inserting synthetic (PBR) objects into a captured scene means the captured
radiance has to *light* them. Current approaches:

| Approach | Example | Limitation |
|---|---|---|
| Bake capture to an environment map / light probe | classic IBL | no parallax, no near-field lighting, no occlusion by the scene |
| Gaussians as individual emitters in a DCC renderer | [GaSLight](https://lvsn.github.io/gaslight/) (ICCV 2025), Blender | authors note it "precludes ... interreflections or scattering" |
| Secondary rays traced into the Gaussian field | 3DGRT-style | BSDF sampling only: the captured scene is found by chance, noisy for diffuse receivers |
| Generative harmonization | [LightHarmony3D](https://arxiv.org/pdf/2603.29209), [D3DR](https://huggingface.co/papers/2503.06740) | not physically based, no convergence guarantee |
| NEE over **2D Gaussian surfels** with lightcuts | [Differentiable light transport with Gaussian surfels](https://arxiv.org/pdf/2509.18497) | closest prior work. Surfels are surfaces (no volumetric transmittance), and the target is inverse rendering / relighting |
| Area emitters + MIS in GS inverse rendering | [AEGIR](https://arxiv.org/pdf/2606.28635) (2026) | surface emitters recovered for relighting, not a captured volume used as the light |
| Gaussians as a path-space radiance cache | [GSCache](https://arxiv.org/html/2507.19718v1) | the Gaussians cache radiance; they are not the emitter |

The search found no work that does **light sampling with MIS from a
volumetric 3D Gaussian radiance field acting as the emitter** for inserted
geometry in an unbiased estimator.

## What makes it hard (and therefore a contribution)

1. **Many lights.** A capture has 10^5 to 10^7 Gaussians. Uniform or power
   selection is useless; this needs a light hierarchy (Conty Estevez & Kulla,
   HPG 2018) whose node bounds handle anisotropic covariance and
   spherical-harmonic (view-dependent) emission. Bounding SH emission over a
   cone of directions is the new technical piece.
2. **Self-occlusion.** A Gaussian's emission reaches the receiver attenuated by
   every other Gaussian in between. The optical depth of one 3D Gaussian along a
   ray is closed form (erf), so transmittance through the field is analytic
   per segment. The light pdf has to stay tractable despite it.
3. **MIS between a line integral and a point sample.** A BSDF ray through the
   field collects emission along its whole length; NEE samples a point. MIS
   needs a common measure. Line-integration MIS for emissive volumes (Simon,
   Hanika, Zirr, Dachsbacher, EGSR 2017; Villemin & Hery, JCGT 2013) is the
   starting point. Extending it to an analytic Gaussian mixture is new.
4. **Radiance versus density.** 3DGS is trained with alpha compositing, not
   volumetric density. Converting opacity to an extinction coefficient is an
   approximation (EVER, Celarek et al.). The method must state which radiance
   model it is unbiased *for*.

## Evaluation plan (what a reviewer will ask for)

- **Baselines:** BSDF sampling into the field (3DGRT-style), environment-map
  baking, per-Gaussian power-based light selection, light BVH without SH
  bounds.
- **Metrics:** equal-time and equal-sample MSE / relMSE and FLIP against
  high-spp references, plus time-to-error curves.
- **Scenes:** public 3DGS captures (Mip-NeRF 360, Tanks & Temples) with
  inserted diffuse, glossy and glass objects.
- **Ablations:** each of 1-3 above switched off on its own.
- **Venues:** EGSR (paper or Symposium track), HPG, JCGT, or a Eurographics short paper.

## How the engine already supports this

- `light_sampler` takes the shading point and normal. A Gaussian light
  hierarchy plugs in behind the same `sample` / `pmf` interface.
- MIS is written against the generic `light::pdf_li`. The analytic tests in
  `tests/test_integrator.cpp` are the template for validating a new emitter
  type.
- The `LightStrategy` switch (`--integrator mis|bsdf|nee`) is the ablation
  harness.
- Deterministic per-pixel random streams make equal-sample comparisons exact.
- `constant_medium` already gives unbiased stochastic transmittance for
  shadow rays.
- Phase 2 measurement pipeline ([BENCHMARKING.md](BENCHMARKING.md)):
  equal-time rendering, relMSE and HDR-FLIP against references with the
  reference noise removed, and convergence plots. A new method is a new entry
  in `benchmarks/run_benchmarks.py`. The paper's results tables come straight
  out of it once the captured-scene benchmarks are added.

## Due diligence before Phase 3

1. Forward citations (Google Scholar) of 3DGRT, GaSLight, the Gaussian-surfels
   NEE paper, AEGIR, and Simon et al. 2017.
2. EGSR, HPG, SIGGRAPH and SIGGRAPH Asia 2025-2026 proceedings: search for
   "Gaussian" + "light sampling" / "many lights" / "emitter".
3. arXiv cs.GR listing since mid-2026 (this area moves monthly).
4. Talk it through with your supervisor before writing code.

If 1-3 turn up the same idea, the fallback with the least overlap is
denoiser-aware adaptive sampling (allocate samples by estimated post-denoise
error). Prior art to position against: Kuznetsov et al. 2018.
