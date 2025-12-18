# Raytracer Improvement Implementation TODO

A comprehensive, detailed implementation checklist for all production-level improvements.

---

## Phase 1: Critical Bug Fixes (Week 1)

### 1.1 Thread-Safe Random Number Generation
- [ ] **File: `src/util/util.h`**
  - [ ] Remove `inline double random_double() { return rand() / (RAND_MAX + 1.0); }`
  - [ ] Add `#include <random>` at top
  - [ ] Add thread_local RNG:
    ```cpp
    inline double random_double() {
        thread_local std::mt19937 gen(std::random_device{}());
        thread_local std::uniform_real_distribution<double> dist(0.0, 1.0);
        return dist(gen);
    }
    
    inline double random_double(double min, double max) {
        return min + (max - min) * random_double();
    }
    ```
  - [ ] Test: Run multithreaded render, verify no artifacts/crashes

### 1.2 Shadow Acne Fix
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] Line ~59: Change `shadowRay.orig = rec.p;` to:
    ```cpp
    shadowRay.orig = rec.p + rec.normal * 0.001;
    ```
  - [ ] Line ~74: Change `lightShadowRay.orig = rec.p;` to:
    ```cpp
    lightShadowRay.orig = rec.p + rec.normal * 0.001;
    ```
  - [ ] Test: Render scene with objects touching ground, verify no black dots

### 1.3 Fix Mesh Hit (Closest Hit, Not First)
- [ ] **File: `src/engine/mesh.cpp`**
  - [ ] Replace current `hit()` function:
    ```cpp
    bool mesh::hit(const ray &r, double t_min, double t_max, hit_record &rec) const {
        hit_record temp_rec;
        bool hit_anything = false;
        auto closest_so_far = t_max;
        
        for (const auto& tri : triangleList) {
            if (tri.hit(r, t_min, closest_so_far, temp_rec)) {
                hit_anything = true;
                closest_so_far = temp_rec.t;
                rec = temp_rec;
            }
        }
        return hit_anything;
    }
    ```
  - [ ] Test: Render overlapping mesh triangles, verify correct depth ordering

### 1.4 Degenerate Triangle Skip
- [ ] **File: `src/engine/triangle.cpp`**
  - [ ] In constructor, set `degenerate = true` and return early (don't add to mesh)
  - [ ] Or: Filter out degenerate triangles in mesh.cpp during load
  - [ ] Test: Load mesh with degenerate triangles, verify no warnings in console

---

## Phase 2: Performance - Per-Mesh BVH (Week 2)

### 2.1 Add BVH to Mesh Class
- [ ] **File: `src/engine/mesh.h`**
  - [ ] Add include: `#include "bvh_node.h"`
  - [ ] Add member: `std::shared_ptr<bvh_node> mesh_bvh;`
  - [ ] Add method: `void buildMeshBVH();`
  - [ ] Add flag: `bool use_mesh_bvh = true;`

### 2.2 Build BVH After Loading
- [ ] **File: `src/engine/mesh.cpp`**
  - [ ] At end of `load()` function, add:
    ```cpp
    if (use_mesh_bvh && !triangleList.empty()) {
        std::vector<std::shared_ptr<hittable>> tri_ptrs;
        tri_ptrs.reserve(triangleList.size());
        for (auto& tri : triangleList) {
            tri_ptrs.push_back(std::make_shared<triangle>(tri));
        }
        mesh_bvh = std::make_shared<bvh_node>(tri_ptrs);
        std::cerr << "Built mesh BVH: " << mesh_bvh->getNodeCount() << " nodes\n";
    }
    ```

### 2.3 Use BVH in Hit Test
- [ ] **File: `src/engine/mesh.cpp`**
  - [ ] Replace `hit()` with:
    ```cpp
    bool mesh::hit(const ray &r, double t_min, double t_max, hit_record &rec) const {
        if (mesh_bvh) {
            return mesh_bvh->hit(r, t_min, t_max, rec);
        }
        // Fallback to linear search
        hit_record temp_rec;
        bool hit_anything = false;
        auto closest_so_far = t_max;
        for (const auto& tri : triangleList) {
            if (tri.hit(r, t_min, closest_so_far, temp_rec)) {
                hit_anything = true;
                closest_so_far = temp_rec.t;
                rec = temp_rec;
            }
        }
        return hit_anything;
    }
    ```
  - [ ] Test: Load high-poly mesh, compare render time before/after

### 2.4 SAH-Based BVH Split (Optional - High Impact)
- [ ] **File: `src/engine/bvh_node.cpp`**
  - [ ] Add SAH cost function:
    ```cpp
    float compute_sah_cost(const aabb& box_left, int count_left,
                           const aabb& box_right, int count_right,
                           const aabb& parent_box) {
        return 1.0f + (box_left.surface_area() * count_left +
                       box_right.surface_area() * count_right) /
                      parent_box.surface_area();
    }
    ```
  - [ ] Replace median split with SAH evaluation in constructor
  - [ ] Test buckets along each axis, pick lowest cost split
  - [ ] Test: Compare render times with and without SAH

---

## Phase 3: Enable OIDN Denoiser (Week 2-3)

### 3.1 Fix Normalization Before Denoising
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] Uncomment OIDN code (around line 286)
  - [ ] Before OIDN, normalize to [0,1]:
    ```cpp
    // Find max value
    float maxVal = 0.0f;
    for (const auto& pixel : bitmap) {
        maxVal = std::max(maxVal, (float)std::max({pixel.x(), pixel.y(), pixel.z()}));
    }
    if (maxVal < 1e-6f) maxVal = 1.0f;
    
    // Normalize
    std::vector<float> normalized(bitmap.size() * 3);
    for (size_t i = 0; i < bitmap.size(); i++) {
        normalized[i*3 + 0] = bitmap[i].x() / maxVal;
        normalized[i*3 + 1] = bitmap[i].y() / maxVal;
        normalized[i*3 + 2] = bitmap[i].z() / maxVal;
    }
    ```
  - [ ] After OIDN, rescale back:
    ```cpp
    for (size_t i = 0; i < bitmap.size(); i++) {
        bitmap[i] = color(output[i*3 + 0] * maxVal,
                          output[i*3 + 1] * maxVal,
                          output[i*3 + 2] * maxVal);
    }
    ```
  - [ ] Test: Render noisy scene (low samples), verify denoising works without overexposure

### 3.2 Add Denoiser Toggle
- [ ] **File: `src/engine/config.h`**
  - [ ] Add: `bool enableDenoiser = true;`
- [ ] **File: `src/main.cpp`**
  - [ ] Add CLI flag: `--no-denoise`
- [ ] Test: Compare with and without denoiser

---

## Phase 4: Rendering Quality (Week 3)

### 4.1 Russian Roulette Path Termination
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] In `TraceRayInternal`, after depth > 3:
    ```cpp
    if (depth <= 0) return color(0,0,0);
    
    // Russian Roulette after depth 3
    if (depth < sceneWorld.GetMaxDepth() - 3) {
        float luminance = 0.2126f * attenuation.x() + 0.7152f * attenuation.y() + 0.0722f * attenuation.z();
        float continue_prob = std::min(0.95f, std::max(0.1f, luminance));
        if (random_double() > continue_prob) {
            return emitted;
        }
        attenuation = attenuation / continue_prob;
    }
    ```
  - [ ] Test: Compare convergence speed on glass/mirror scenes

### 4.2 Cosine-Weighted Hemisphere Sampling
- [ ] **File: `src/util/vec3.h`**
  - [ ] Add function:
    ```cpp
    inline vec3 random_cosine_direction() {
        double r1 = random_double();
        double r2 = random_double();
        double z = sqrt(1 - r2);
        double phi = 2 * PI * r1;
        double x = cos(phi) * sqrt(r2);
        double y = sin(phi) * sqrt(r2);
        return vec3(x, y, z);
    }
    ```
- [ ] **File: `src/engine/lambertian.cpp`**
  - [ ] Consider using cosine-weighted instead of uniform
  - [ ] Adjust attenuation for PDF: `attenuation = albedo * PI;` (since PDF = cos/π)

### 4.3 Importance Sampling for Point Lights
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] Weight light contribution by solid angle:
    ```cpp
    double solid_angle = light->radius * light->radius * PI / (lightDist * lightDist);
    result += light->lightColor * cosTheta * light->intensity * solid_angle;
    ```

---

## Phase 5: Feature Additions (Week 4)

### 5.1 Normal Mapping Support
- [ ] **File: `src/engine/material.h`**
  - [ ] Add to base class: `std::shared_ptr<texture> normal_map;`
  - [ ] Add method: `vec3 perturb_normal(const hit_record& rec) const;`
- [ ] **File: `src/engine/hittable.h`**
  - [ ] Add to `hit_record`: `vec3 tangent, bitangent;`
- [ ] **File: `src/engine/triangle.cpp`**
  - [ ] Compute tangent/bitangent from UV derivatives during hit
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] Apply normal perturbation before scattering

### 5.2 Bilinear Texture Filtering
- [ ] **File: `src/engine/image_texture.h`**
  - [ ] In `value()`, instead of nearest neighbor:
    ```cpp
    float fx = u * width - 0.5f;
    float fy = v * height - 0.5f;
    int x0 = floor(fx), y0 = floor(fy);
    int x1 = x0 + 1, y1 = y0 + 1;
    float tx = fx - x0, ty = fy - y0;
    
    color c00 = sample(x0, y0);
    color c10 = sample(x1, y0);
    color c01 = sample(x0, y1);
    color c11 = sample(x1, y1);
    
    return (1-tx)*(1-ty)*c00 + tx*(1-ty)*c10 + (1-tx)*ty*c01 + tx*ty*c11;
    ```

### 5.3 Depth of Field
- [ ] **File: `src/engine/camera.h`**
  - [ ] Add members: `double aperture, focus_dist;`
  - [ ] Add: `vec3 u, v, w; double lens_radius;`
- [ ] **File: `src/engine/camera.cpp`**
  - [ ] In constructor: `lens_radius = aperture / 2;`
  - [ ] In `get_ray()`:
    ```cpp
    vec3 rd = lens_radius * random_in_unit_disk();
    vec3 offset = u * rd.x() + v * rd.y();
    return ray(origin + offset, 
               lower_left_corner + s*horizontal + t*vertical - origin - offset);
    ```

---

## Phase 6: UI Improvements (Week 5)

### 6.1 Progressive Rendering
- [ ] **File: `src/ui/services/RendererService.cpp`**
  - [ ] Render in passes instead of tiles:
    ```cpp
    for (int pass = 0; pass < total_passes; pass++) {
        render_one_sample_per_pixel();
        emit frameReady(accumulated_image / (pass + 1));
    }
    ```
- [ ] **File: `src/ui/views/RenderViewportWidget.cpp`**
  - [ ] Update display on each pass

### 6.2 Render Region Selection
- [ ] **File: `src/ui/views/SceneViewport.cpp`**
  - [ ] Add rectangle selection tool
  - [ ] Store selected region bounds
- [ ] **File: `src/engine/render_runner.cpp`**
  - [ ] Add optional region parameters to `RenderSceneToBitmap()`

### 6.3 Undo/Redo System
- [ ] **File: `src/ui/models/` - Create new file `UndoStack.h`**
  - [ ] Implement command pattern for scene modifications
- [ ] **File: `src/ui/models/SceneDocument.cpp`**
  - [ ] Wrap all modification methods with undo commands

---

## Phase 7: Testing & Quality (Week 6)

### 7.1 Unit Tests
- [ ] Create `tests/` directory
- [ ] Add test files:
  - [ ] `tests/test_vec3.cpp` - Vector operations
  - [ ] `tests/test_intersections.cpp` - Ray-primitive tests
  - [ ] `tests/test_bvh.cpp` - BVH construction/traversal
  - [ ] `tests/test_materials.cpp` - Material scattering
- [ ] Add CMake target for tests
- [ ] Add GitHub Actions CI workflow

### 7.2 Benchmark Suite
- [ ] Create `benchmarks/` directory
- [ ] Create standard test scenes:
  - [ ] `cornell_box.xml` - Classic reference
  - [ ] `many_spheres.xml` - BVH stress test
  - [ ] `high_poly_mesh.xml` - Mesh performance
- [ ] Add timing scripts

### 7.3 Documentation
- [ ] Update README.md with:
  - [ ] Installation instructions for all platforms
  - [ ] Full CLI reference
  - [ ] Scene XML format documentation
  - [ ] Material parameter reference

---

## Completion Checklist

- [ ] All Phase 1 items complete
- [ ] All Phase 2 items complete
- [ ] All Phase 3 items complete
- [ ] All Phase 4 items complete
- [ ] All Phase 5 items complete
- [ ] All Phase 6 items complete
- [ ] All Phase 7 items complete
- [ ] All tests passing
- [ ] Documentation updated
- [ ] Release notes written
