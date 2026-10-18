# Literature review: captured Gaussian scenes, inserted objects, light transport

Second check, 18 October 2026. It supports
[RESEARCH_DIRECTION.md](RESEARCH_DIRECTION.md) and replaces the first, shorter
check made there.

**Method and limits.** Web searches over 2024-2026 work: arXiv, CVPR, ICCV,
EGSR, SIGGRAPH and SIGGRAPH Asia listings, vendor documentation. Each entry is
summarized from its abstract, project page or documentation, **not from the
full paper**: arXiv and the paper databases were unreachable from the machine
this was written on. Entries marked **verify** make or break a claim in the
research direction and must be checked in the full text before any
implementation is committed to.

## 1. Inserting objects into captured radiance fields

| Work | How the inserted object is lit | Object's effect on the captured scene | Ground truth |
|---|---|---|---|
| [LightHarmony3D](https://arxiv.org/abs/2603.29209) (2026) | one generated 360° HDR environment map per insertion point | shadow ratio map from a PBR render of a proxy mesh, multiplied onto the 3DGS render | **LH3D-Bench**: synthetic scenes path traced with and without the object, 3DGS trained from Blender renders via a Blender-to-COLMAP export; PSNR / SSIM / LPIPS |
| [DOT3D](https://arxiv.org/abs/2606.22481), Violante, Kopanas, Franke, Philip, Drettakis (EGSR 2026) | diffusion model harmonizes each rendered view, then distilled into the 3DGS object | via the diffusion model | 30 synthetic Blender scenes (Cycles, 128 spp, LDR sRGB) |
| [D3DR](https://arxiv.org/abs/2503.06740) (2025) | object Gaussians optimized with a diffusion (score distillation) objective | via the diffusion model | relighting PSNR / SSIM |
| [GaSLight](https://arxiv.org/abs/2504.10809) (ICCV 2025) | HDR Gaussians imported into Blender Cycles; Gaussians above intensity 1 become emitters, the rest diffuse surfaces | cast shadows through Cycles | authors: "precludes ... interreflections or scattering" |
| [Ye et al.](https://arxiv.org/abs/2310.05837) (TVCG 2024), NeRF | NeRF as near-field light plus an estimated distant environment | soft shadows from a precomputed spherical SDF | physical 3D-printed objects in real photos |
| [3DGRT / 3DGRUT](https://github.com/nv-tlabs/3dgrut) (SIGGRAPH Asia 2024, CVPR 2025) | secondary rays from mesh into the particles (BSDF sampling only) | hard shadows, described elsewhere as dimming the Gaussians, a heuristic | none for insertion |
| [Omniverse RTX particle fields](https://docs.omniverse.nvidia.com/materials-and-rendering/latest/particle-fields.html) | scene lights (dome, manual); docs do not say splats light meshes | splats can receive mesh shadows; proxy-mesh matte for NuRec volumes | none |
| [Arnold 7.5.2](https://www.cgchannel.com/2026/06/autodesk-releases-arnold-7-5-2-with-support-for-gaussian-splatting/) (June 2026) | splat shader has emission (captured color) and diffuse (relit) lobes | path-traced relighting of the splats | none |
| [TRON](https://arxiv.org/abs/2606.11314), [ComGS](https://arxiv.org/abs/2510.07729) (2025-26) | ray tracer as guidance for a neural renderer; surface octahedral probes | harmonized insertion; probe-based shadows | per paper |

**Consequence.** A path-traced paired benchmark for mesh insertion into 3DGS
exists (LH3D-Bench). A new benchmark on its own is not a contribution.
What none of the summaries mention: spatially varying near-field lighting
from the capture itself, interreflection between object and capture (object
color on the captured floor), HDR error metrics, or attribution of error to
its source.

## 2. Inverse rendering and path tracing inside Gaussian scenes

| Work | What it recovers | Light transport | Relevance |
|---|---|---|---|
| [PTIR-GS](https://arxiv.org/abs/2606.09606) (2026, [code](https://github.com/junkzhu/PTIR-GS)) | materials + spherical-Gaussian environment | forward and backward pass through one ray tracer, global illumination; Gaussians coexisting with meshes, environment and analytic lights | **verify**: whether captured radiance is ever light-sampled for an inserted mesh |
| [EAG-PT](https://arxiv.org/abs/2601.23065) (2026) | emitters separated by 2D emission masks; emitter radiance and diffuse reflectance | 2D Gaussians as transport proxy; single bounce for reconstruction, multi-bounce path tracing after edits, no mesh conversion | **verify**: how emitter Gaussians are sampled in the multi-bounce pass. Closest to "the capture lights the edit", but recovers explicit emitters rather than using all captured radiance |
| [AEGIR](https://arxiv.org/abs/2606.28635) (2026) | local area emitters inside a Gaussian scene | MIS with area emitters | better insertion lighting from explicit emitters |
| [Gaussian surfels radiosity](https://arxiv.org/abs/2509.18497) (2025) | relightable 2D surfels | NEE over surfel kernels with weighted reservoir sampling, lightcuts-inspired | closest many-light-over-Gaussians prior work; surfaces, no volumetric transmittance |
| [R3DG](https://arxiv.org/abs/2311.16043) (2023), [IRGS](https://openaccess.thecvf.com/content/CVPR2025/papers/Gu_IRGS_Inter-Reflective_Gaussian_Splatting_with_2D_Gaussian_Ray_Tracing_CVPR_2025_paper.pdf) (CVPR 2025), [GI-GS](https://arxiv.org/abs/2410.02619) | BRDF per Gaussian, environment light | ray-traced visibility, inter-reflection | composition with shadows; global inverse rendering by optimization |

**Consequence.** Full inverse rendering of Gaussian scenes is crowded. A
local, forward Monte Carlo reflectance estimate restricted to the region an
insertion changes (no optimization) is not described in these summaries, but
it is the same idea as Debevec's local-scene reflectance estimation (1998),
moved into a radiance field. Claim it as an enabling component, not a
headline.

## 3. Gaussians as volumetric primitives in a path tracer

- [Don't Splat your Gaussians](https://arxiv.org/abs/2405.15425), Condor et
  al. (TOG 2025): kernel primitives with emission, cross-section and phase
  function in a volumetric path tracer; closed-form transmittance and
  free-flight sampling. **verify**: whether emissive primitives are sampled as
  lights for other geometry. The summaries only describe NEE through
  primitives (transmittance of shadow rays toward other lights). If they also
  light-sample emissive primitives, contribution C1 narrows to the light
  hierarchy with SH bounds, the line-integral MIS and the evaluation.
- [EVER](https://arxiv.org/abs/2410.01804) (ICCV 2025),
  [stochastic ray tracing of transparent Gaussians](https://arxiv.org/abs/2504.06598)
  (EGSR 2025), [stochastic ray tracing for 3DGS reconstruction](https://arxiv.org/abs/2603.23637)
  (2026), [GRay](https://arxiv.org/abs/2606.30869) (2026): exact or stochastic
  volumetric rendering of Gaussians; speed and fidelity, not light sampling.
- [Celarek et al.](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70032) (CGF 2025),
  [Volumetrically consistent 3DGS](https://chinmay0301.github.io/vol3dgs/)
  (CVPR 2025): error of the alpha-blending approximation, needed to state
  which radiance model the estimator is unbiased for.

## 4. Gaussians as a light-transport representation (not as emitters)

- [GSCache](https://arxiv.org/abs/2507.19718) (2025): Gaussians as a path-space radiance cache.
- [Gaussian Light Transport](https://arxiv.org/abs/2609.11430), Attimont,
  Subr, Soler (SIGGRAPH Asia 2026): global illumination as a 13D Gaussian
  mixture fitted to the rendering-equation residual. A cache for synthetic
  scenes, not captured radiance used as a light.

## 5. Sampling many lights and emissive volumes

- Conty Estevez & Kulla, [light tree with adaptive splitting](https://dl.acm.org/doi/abs/10.1145/3233305) (HPG 2018).
- Tokuyoshi et al., [hierarchical light sampling with accurate spherical Gaussian lighting](https://dl.acm.org/doi/10.1145/3680528.3687647)
  (SIGGRAPH Asia 2024): spherical-Gaussian node importance. The nearest tool
  for bounding view-dependent emission per node.
- Simon, Hanika, Zirr, Dachsbacher, [line integration for heterogeneous emissive volumes](https://cg.ivd.kit.edu/fnee.php)
  (EGSR 2017), forward NEE.
- Villemin & Hery, [MIS for emissive effects](https://graphics.pixar.com/library/MISEmissive/paper.pdf) (2013).
- Lin, Wyman, Yuksel, [volumetric ReSTIR](https://graphics.cs.utah.edu/research/projects/volumetric-restir/volumetric_restir.pdf) (2021).
- Karlík et al., MIS compensation (SIGGRAPH Asia 2019): reshape a light
  pdf for the case where it is combined with BSDF sampling.

No work found builds a light hierarchy over volumetric, view-dependent
(spherical-harmonic) emitters.

## 6. Light probes obtained by inserting a known object

- Debevec, *Rendering synthetic objects into real scenes* (SIGGRAPH 1998):
  light probe, local-scene reflectance estimate, differential rendering.
  The template for contributions C2 and C3.
- [DiffusionLight](https://openaccess.thecvf.com/content/CVPR2024/html/Phongthawee_DiffusionLight_Light_Probes_for_Free_by_Painting_a_Chrome_Ball_CVPR_2024_paper.html)
  (CVPR 2024) and follow-ups: a diffusion model paints a chrome ball into a
  single image to estimate lighting. Hallucinated, single view.

## To verify before implementation (in this order)

1. PTIR-GS and EAG-PT: is captured radiance light-sampled when an inserted
   object is rendered? Read the method sections and the PTIR-GS code.
2. Condor et al. 2025: are emissive primitives light-sampled?
3. LightHarmony3D: which effects LH3D-Bench measures, LDR or HDR, release
   status, scene count.
4. GaSLight: how Cycles samples thousands of emissive Gaussians (light
   tree?), to build a faithful baseline.
5. Forward citations of all of the above (Google Scholar), the SIGGRAPH Asia
   2026 program (December) and arXiv cs.GR, monthly until submission.
