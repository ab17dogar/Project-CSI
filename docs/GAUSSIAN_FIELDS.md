# Gaussian fields: 3DGS captures inside the path tracer

Stage 1 of [RESEARCH_DIRECTION.md](RESEARCH_DIRECTION.md). The renderer
loads a 3D Gaussian Splatting (3DGS) capture and path traces it together with
ordinary geometry: the capture is seen directly, lights the objects through
BSDF rays, and blocks light, including on shadow rays.

## Using it

```xml
<Objects>
  <GaussianField file="capture.ply">     <!-- relative to the scene file -->
    <Position x="0" y="0" z="0"/>
    <Scale value="1"/>                    <!-- uniform -->
    <Rotation x="0" y="0" z="0"/>         <!-- degrees, applied as Rx Ry Rz -->
  </GaussianField>
  <Sphere> ... </Sphere>                  <!-- inserted geometry as usual -->
</Objects>
```

The file is the standard binary `.ply` written by 3DGS trainers (the
reference implementation, gsplat, nerfstudio's splatfacto): `x y z`,
`f_dc_*`, `f_rest_*` (spherical harmonics of degree 0 to 3), `opacity`
(logit), `scale_*` (log), `rot_*` (quaternion w, x, y, z). At most one field
per scene; the transform places it in the scene, and view-dependent colour is
evaluated in the field's own frame, so rotating a capture needs no rotation
of its SH coefficients.

## Radiance model

The 3DGRT particle model (Moenne-Loccoz et al. 2024). Gaussian i meets a ray
at its point of maximum response t_i with opacity
alpha_i = o_i exp(-d_i^2 / 2) (d_i the Mahalanobis distance of the ray to the
mean there) and radiance c_i(direction) = SH + 0.5, clamped at 0. Along a
segment the field adds

    L = sum_i T_i alpha_i c_i,   T_i = prod over t_j < t_i of (1 - alpha_j)

and lets through T = prod_i (1 - alpha_i) of whatever lies behind it. As in
the reference rasterizer, opacities are clamped to 0.99 and responses below
1/255 ignored (each Gaussian's bounding box is sized to exactly that
cut-off); unlike it, there is no early ray termination, so the sum is exact
for this model. The field emits and absorbs; it does not scatter.

In the integrator, every ray segment (camera, bounce) collects the field's
emission up to the next surface and is attenuated by its transmittance;
shadow rays are attenuated too. Nothing light-samples the field yet, so its
emission has full weight under all three strategies (`--integrator mis |
bsdf | nee`), and all three stay unbiased. Light sampling of the field is
stage 2 (the design is in RESEARCH_DIRECTION.md, contribution C1).

## Tests

`tests/test_gaussians.cpp`:

| Test | Checks |
|---|---|
| `sh_basis_is_orthonormal_and_matches_reference` | Gram matrix of the 16 basis functions by quadrature; spot values against the reference formulas; clamp at 0 |
| `gaussian_ply_loads_like_the_reference` | a file in the reference layout: sigmoid opacity, clamp, log scales, unnormalized quaternion, channel-major `f_rest`, dropping of invisible Gaussians, loading and placing through a scene file |
| `gaussian_alpha_matches_closed_form` | one rotated anisotropic Gaussian: alpha through the mean and off-axis, segment limits |
| `gaussian_field_matches_brute_force_and_transforms` | 300 random Gaussians, 400 rays: acceleration structure + sorting equals brute force; a transformed field equals the untransformed one in local coordinates |
| `gaussian_field_lights_a_floor` | the field as the only light: floor radiance against quadrature, all strategies |
| `gaussian_field_shadows_a_point_light` | a black Gaussian on a shadow ray passes exactly 1 - opacity |
| `gaussian_field_attenuates_what_is_behind_it` | an emitter seen through a Gaussian |

Mutation-tested: compositing without sorting, dropping the path attenuation
or the shadow-ray transmittance, a transposed whitening matrix and
coefficient-major `f_rest` are each caught by the test written for them.

## A Gaussian scene without a GPU

Training 3DGS needs a CUDA GPU. To run the insertion loop without one,
`benchmarks/points_to_gaussians.py` builds an *unoptimized* stand-in from a
dataset ([DATASETS.md](DATASETS.md)): every few training pixels become a
flat Gaussian on the visible surface, oriented by the normal map, sized by
the pixel footprint (stretched along the view direction at grazing angles)
and coloured with the pixel's HDR radiance. It is crude (blurred edges,
emitters grown at grazing angles, view-dependent effects baked per view),
but it has the right structure: a trained 3DGS `.ply` of the same dataset
replaces it one to one.

`benchmarks/insertion_demo.py` then renders a test view of the Gaussian
scene alone and with the dataset's inserted objects, the field being the
only light, and scores both against the path-traced ground truth in three
regions: the object, its influence region (where the ground truth with and
without the object differ: shadow, reflected light) and the rest.

![Insertion into a Gaussian stand-in of the Cornell box](figures/insertion_demo.png)

`cornell_box` with the blue sphere: 24 training views at 1024 spp, a stand-in
of 154,711 Gaussians (`--stride 3`), test view 1 at 256 spp, 3.5 minutes for
both renders on 4 cores. relMSE against ground truth, mean (median):

| Comparison | object | influence | rest |
|---|---:|---:|---:|
| Gaussian scene vs. the scene it reconstructs | 0.0175 (0.0013) | 0.0092 (0.0007) | 62.9 (0.0017) |
| Gaussian scene + sphere vs. ground truth | 0.0367 (0.0021) | 0.102 (0.0009) | 63.5 (0.0015) |
| Ground truth without the sphere vs. with it | 0.427 (0.13) | 0.110 (0.0022) | 0.0002 (0.00002) |

- **Object.** The sphere, lit only by the Gaussian scene, has 12x lower error
  than leaving it out. What remains is mostly noise: the stand-in's lamp is a
  cluster of small bright Gaussians that only BSDF rays find (the speckled
  highlight). That is contribution C1's target.
- **Influence.** 0.102 against 0.110 for ignoring the sphere altogether: the
  sphere's shadow and the blue light it reflects onto the floor and the red
  wall are missing, because the field does not receive light. That is
  contribution C2's target, now measured.
- **Rest.** The median (0.0017) is the stand-in's reconstruction error. The
  mean is 63 because 1% of the pixels carry 96% of it: where the stand-in's
  enlarged lamp (luminance about 9) covers dark ceiling (about 0.08). A mean
  alone would misreport the reconstruction, so the demo reports both.

Reproduce:

```bash
python3 benchmarks/make_dataset.py cornell_box --insert benchmarks/inserts/cornell_blue_sphere.xml \
    --poses jitter --train 24 --test 4 --spp 1024 --width 256 --exposure 1.5
python3 benchmarks/points_to_gaussians.py datasets/cornell_box --stride 3
python3 benchmarks/insertion_demo.py datasets/cornell_box --view 1 --spp 256
```

## Limitations

- One field per scene; the field does not scatter light, so the inserted
  object's shadow and reflected light do not appear on the capture
  (contribution C2).
- No light sampling of the field yet: small bright Gaussians (a captured
  lamp) are found by BSDF rays only, which is noisy (contribution C1).
- The particle model is what a trained 3DGS shows when ray traced, not a
  continuous volume; a ray's result depends on the order of maximum
  responses, as in 3DGRT.
- CPU traversal: a binary median-split BVH over the Gaussians' support
  boxes and a sort per ray. About 15 s for 256x256 at 16 spp through 155k
  Gaussians on 4 cores. Fast enough for experiments, not for interaction.
