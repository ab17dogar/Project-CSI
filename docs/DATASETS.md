# Multi-view datasets for reconstruction and object insertion

`benchmarks/make_dataset.py` path traces a scene from many viewpoints and
writes a dataset that 3D Gaussian Splatting (3DGS) trainers read directly.
With `--insert`, it also renders the test views with extra objects added,
which gives exact ground truth for inserting those objects into the
reconstruction. This is Stage 0 of
[RESEARCH_DIRECTION.md](RESEARCH_DIRECTION.md): the closed loop of
contribution C4 (render, reconstruct, insert, compare).

## Quick start

```bash
cmake --build build
python3 benchmarks/make_dataset.py cornell_box \
    --insert benchmarks/inserts/cornell_blue_sphere.xml \
    --poses jitter --train 100 --test 20 --spp 512
# -> datasets/cornell_box/
```

`--poses orbit` (default) places cameras on a Fibonacci spiral around the
scene camera's target (`--center`, `--radius`, `--elev MIN MAX`), with test
views interleaved between training views. `--poses jitter` perturbs the
scene's own camera (`--jitter`, a fraction of its distance to the target),
for closed rooms like the Cornell box that cannot be orbited from outside.

## Layout

| Path | Content |
|---|---|
| `transforms_train.json`, `transforms_test.json` | intrinsics and one camera-to-world matrix per frame |
| `train/r_000.png`, `.pfm` | training views: 8-bit sRGB at `--exposure`, and linear HDR |
| `test/` | held-out views of the unmodified scene |
| `test_inserted/r_000.png`, `.pfm` | the test views with the objects of `--insert` added: ground truth |
| `test_inserted/r_000_mask.png` | pixels where an inserted object is directly visible |
| `geometry/<split>/r_000_depth.pfm`, `_normal.pfm` | z-depth and world-space normal of the first hit (0 where nothing is hit) |
| `points3d.ply` | initial point cloud, back-projected from training depth with colors |
| `insert.xml` | copy of the inserted objects |
| `dataset.json` | renderer commit, machine, settings, every pose and seed |

Training uses only `train/`. A method under test inserts the objects of
`insert.xml` into its reconstruction and renders the poses of
`transforms_test.json`; compare its output with `test_inserted/` (HDR:
`rt_compare`), and use `test/` versus `test_inserted/` to find the region the
objects changed (shadow, color bleeding).

## Conventions

- **Poses.** `transform_matrix` is camera-to-world with the columns right,
  up, backward, position: the NeRF / Blender (OpenGL) convention, and exactly
  the renderer's camera frame (`camera.cpp` looks along −w).
- **Intrinsics.** `camera_angle_x` and `camera_angle_y` are the full fields
  of view. The renderer maps the vertical field of view to the image height
  and `aspect × ` that to the width, so pixels are square only when
  width / height equals the scene's aspect ratio; `fl_x` and `fl_y` are exact
  either way, with the principal point at the image center.
- **Depth** is distance along the viewing axis, not along the ray.
- **Seeds.** Each view has its own render seed, so noise is independent
  across views. A `test_inserted` view reuses its `test` view's seed, so the
  two share random numbers: a sample whose path never meets the inserted
  objects cancels exactly in their difference. In a closed room almost every
  pixel has some sample that does meet them, so few pixels are identical,
  but the difference is far less noisy. Measured on `cornell_box` with
  the blue sphere (one view, 256 spp): the mean squared difference outside
  the sphere is 214x smaller than between independently seeded renders, and
  1248x smaller in the rows far from it.

## Checking a dataset

```bash
python3 benchmarks/check_dataset.py datasets/cornell_box
```

reads the poses the way the reference 3DGS loader does, projects
`points3d.ply` into every training view and compares with the rendered depth
maps. Points may be occluded, but none may float in front of the visible
surface; a flipped axis, flipped rows or a field-of-view mix-up puts a
quarter to half of them there. CI runs it on every build. Edges thinner than a
pixel let a small fraction through at low resolution (under 1% at 128 px),
hence the 2% default tolerance.

## Training 3DGS on it

The reference implementation (graphdeco-inria `gaussian-splatting`)
recognizes the layout by `transforms_train.json`, and loads `points3d.ply`
instead of initializing points at random in a unit cube:

```bash
python train.py -s /path/to/datasets/cornell_box --eval
```

Other trainers that read the NeRF-synthetic layout work too. Training needs
a CUDA GPU; the dataset itself is CPU only.

## Caveats

- **LDR training data.** The PNGs are clipped at 1 after `--exposure`, like
  a camera. Light sources clip; choose the exposure so surfaces do not. The
  `.pfm` files keep the full range for HDR-aware trainers and for
  evaluation.
- **Noise.** Views are not denoised (a denoiser's bias would be baked into
  the reconstruction). Residual Monte Carlo noise is learned as texture; use
  enough samples (512 spp is a start for the Cornell box; caustics need far
  more).
- **Insert files.** Relative paths in the insert file (meshes, textures)
  resolve against the base scene's directory. Material names must be new.
- **Masks** mark direct visibility only, not reflections or refractions of the
  inserted objects.
