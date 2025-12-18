# Production-Level Raytracer Improvements

Based on my thorough analysis of your codebase, here are prioritized improvements to make your raytracer production-ready.

---

## 🔴 Critical (Must-Have for Production)

### 1. Performance Optimizations

#### a) Enable SIMD Vectorization
Your `vec3` class uses `double[3]` - switch to SIMD intrinsics for 2-4x speedup:

```cpp
// Current (slow)
class vec3 { double e[3]; };

// Production (fast) - use GLM's aligned types
#include <glm/simd/common.h>
using vec3 = glm::dvec3;  // Already SIMD-optimized
```

#### b) Fix the Mesh Hit Test (Major Bottleneck!)
In `mesh.cpp`, your `hit()` does linear search through triangles:

```cpp
// Current - O(n) per mesh!
for (size_t i = 0; i < triangleList.size(); i++) {
    if (triangleList[i].hit(r, t_min, t_max, rec)) {
        return true;  // BUG: Returns on first hit, not closest!
    }
}
```

**Fix**: Build per-mesh BVH and find closest hit:
```cpp
bool mesh::hit(...) const {
    if (!mesh_bvh) return false;
    return mesh_bvh->hit(r, t_min, t_max, rec);
}
```

#### c) Implement SAH-Based BVH (Surface Area Heuristic)
Your current BVH uses median split. SAH gives 20-40% faster traversal:

```cpp
// Instead of longest_axis median split
float best_cost = INF;
for (int axis = 0; axis < 3; axis++) {
    for (int bucket = 0; bucket < NUM_BUCKETS; bucket++) {
        float cost = compute_sah_cost(axis, bucket);
        if (cost < best_cost) { best_split = {axis, bucket}; }
    }
}
```

---

### 2. Fix Critical Bugs

#### a) Shadow Acne Prevention
Your shadow ray epsilon is too small for some scenes:
```cpp
// render_runner.cpp line 61
shadowRay.orig = rec.p;  // Acne-prone

// Fix: Offset along normal
shadowRay.orig = rec.p + rec.normal * 0.001;
```

#### b) Degenerate Triangle Handling
`triangle.cpp` warns but still processes degenerate triangles. Skip them entirely.

#### c) Thread-Safe Random Number Generation
Your `random_double()` uses global state - causes race conditions:
```cpp
// util.h - not thread-safe
inline double random_double() { return rand() / (RAND_MAX + 1.0); }

// Fix: Use thread-local RNG
thread_local std::mt19937 gen(std::random_device{}());
inline double random_double() {
    static thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(gen);
}
```

---

### 3. Memory & Stability

#### a) Smart Pointer Audit
Some raw pointers in UI code could leak. Replace with `std::unique_ptr` or `std::shared_ptr`.

#### b) Exception Safety
`LoadScene()` doesn't handle malformed XML gracefully. Add try-catch blocks.

#### c) Large Scene Support
Current `vector<triangle>` doesn't scale. Use memory-mapped files for scenes with millions of triangles.

---

## 🟡 Important (Highly Recommended)

### 4. Rendering Quality

#### a) Importance Sampling for Lights
Your point light sampling is uniform. Use cosine-weighted sampling:
```cpp
// Instead of uniform hemisphere
vec3 direction = cosine_weighted_sample(rec.normal);
float pdf = dot(direction, rec.normal) / PI;
```

#### b) Multiple Importance Sampling (MIS)
Combine BSDF and light sampling with MIS weights for faster convergence:
```cpp
color result = (bsdf_sample * bsdf_weight + light_sample * light_weight) 
             / (bsdf_pdf + light_pdf);
```

#### c) Russian Roulette Path Termination
Instead of fixed `max_depth`, probabilistically terminate paths:
```cpp
if (depth > 3) {
    float continue_prob = std::min(0.95f, luminance(attenuation));
    if (random_double() > continue_prob) return color(0,0,0);
    attenuation /= continue_prob;
}
```

#### d) Stratified Sampling
Replace random jitter with stratified samples for lower variance:
```cpp
for (int sy = 0; sy < sqrt_spp; sy++) {
    for (int sx = 0; sx < sqrt_spp; sx++) {
        double u = (x + (sx + random_double()) / sqrt_spp) / width;
        double v = (y + (sy + random_double()) / sqrt_spp) / height;
    }
}
```

---

### 5. Enable OIDN Denoiser (Currently Disabled!)
I see OIDN is commented out in `render_runner.cpp`:
```cpp
// OIDN denoising disabled - was causing overexposure issues
```

**Fix**: Normalize to [0,1] before denoising, then rescale:
```cpp
// Before OIDN
float maxVal = find_max_channel(bitmap);
for (auto& pixel : bitmap) pixel /= maxVal;

// Run OIDN
device.execute();

// After OIDN  
for (auto& pixel : output) pixel *= maxVal;
```

---

### 6. Add Missing Features

| Feature | Impact | Effort |
|---------|--------|--------|
| **Normal mapping** | Huge visual improvement | Medium |
| **Texture filtering (bilinear)** | Removes pixelation | Low |
| **Area lights** | Soft shadows | Medium |
| **Depth of field** | Cinematic look | Low |
| **Motion blur** | Time-based `ray.tm` already exists! | Medium |
| **Volumetric scattering** | Fog, smoke | High |

---

## 🟢 Nice-to-Have (Polish)

### 7. Code Quality

#### a) Use Modern C++ Features
```cpp
// Replace raw loops with ranges (C++20)
for (const auto& obj : objects | std::views::filter(is_visible)) { ... }

// Use constexpr where possible
constexpr double PI = 3.14159265358979323846;
```

#### b) Add Unit Tests
Create test suite for critical algorithms:
- `vec3` operations
- Ray-primitive intersections
- BVH construction
- Material scattering

#### c) Consistent Naming
Mix of styles: `hit_record` vs `SceneNode`, `SAMPLES_PER_PIXEL` vs `samplesPerPixel`. Pick one.

---

### 8. UI Improvements

#### a) Real-Time Preview
Render at 1/4 resolution with 1 sample for interactive viewport.

#### b) Progressive Rendering
Instead of tile-by-tile, show progressively refined full image:
```cpp
for (int pass = 0; pass < total_passes; pass++) {
    for (each pixel) {
        color += trace_one_sample();
    }
    update_display(color / (pass + 1));
}
```

#### c) Render Region Selection
Let users render only selected area for faster iteration.

#### d) Undo/Redo System
`SceneDocument` changes aren't tracked. Add command pattern.

---

### 9. File Format Improvements

#### a) Binary Scene Format
XML parsing is slow for large scenes. Add binary format:
```cpp
struct SceneHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t num_objects;
    uint32_t num_materials;
};
```

#### b) Support More Formats
- **glTF 2.0**: Already started! Complete PBR material import
- **FBX**: Industry standard
- **USD**: Modern pipeline format

---

### 10. GPU Acceleration (Future)

Consider CUDA/OptiX or Metal for 100x+ speedup:
```cpp
// CPU: 60 seconds for 1080p @ 200 samples
// GPU: < 1 second for same quality
```

Libraries: NVIDIA OptiX, AMD RadeonRays, Intel Embree

---

## 📊 Priority Matrix

| Priority | Improvement | Time | Impact |
|----------|-------------|------|--------|
| 1 | Per-mesh BVH | 2h | 🔥🔥🔥🔥🔥 |
| 2 | Thread-safe RNG | 30m | 🔥🔥🔥🔥 |
| 3 | Shadow bias fix | 15m | 🔥🔥🔥 |
| 4 | SAH BVH | 4h | 🔥🔥🔥🔥 |
| 5 | Enable OIDN | 1h | 🔥🔥🔥🔥 |
| 6 | Importance sampling | 3h | 🔥🔥🔥🔥 |
| 7 | Normal mapping | 2h | 🔥🔥🔥 |
| 8 | Progressive rendering | 2h | 🔥🔥🔥 |
| 9 | Unit tests | 4h | 🔥🔥 |
| 10 | GPU port | 40h+ | 🔥🔥🔥🔥🔥 |

---

## 🚀 Recommended Implementation Order

1. **Week 1**: Fix bugs (shadow acne, thread-safe RNG, mesh hit closest)
2. **Week 2**: Per-mesh BVH + SAH optimization
3. **Week 3**: Enable OIDN + importance sampling
4. **Week 4**: Normal mapping + texture filtering
5. **Week 5**: Progressive rendering + UI polish
6. **Week 6+**: GPU acceleration research

---

Would you like me to implement any of these improvements?
