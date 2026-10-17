// BVH correctness: identical answers to brute-force intersection.
#include "test_framework.h"
#include "test_helpers.h"

#include "engine/bvh_node.h"
#include "engine/lambertian.h"
#include "engine/mesh.h"
#include "engine/quad.h"
#include "engine/sampling.h"
#include "engine/sphere.h"
#include "engine/triangle.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace {

point3 random_point(double lo, double hi) {
  return point3(random_double(lo, hi), random_double(lo, hi), random_double(lo, hi));
}

ray random_ray() {
  const point3 o = random_point(-6, 6);
  const point3 target = random_point(-2, 2);
  return ray(o, unit_vector(target - o));
}

// Compare closest-hit and any-hit queries of `fast` against brute force.
void compare_against_linear(const std::shared_ptr<world> &w, int n_rays, const char *what) {
  int mismatches = 0, hits = 0;
  for (int i = 0; i < n_rays; ++i) {
    const ray r = random_ray();
    const double t_max = (i % 3 == 0) ? random_double(0.5, 8.0) : INF;
    hit_record a, b;
    w->pconfig->acceleration = AccelerationMethod::BVH;
    const bool ha = w->hit(r, 1e-9, t_max, a);
    const bool any_a = w->hit_any(r, 1e-9, t_max);
    w->pconfig->acceleration = AccelerationMethod::LINEAR;
    const bool hb = w->hit(r, 1e-9, t_max, b);
    const bool any_b = w->hit_any(r, 1e-9, t_max);
    bool ok = ha == hb && any_a == any_b && ha == any_a;
    if (ok && ha)
      ok = std::fabs(a.t - b.t) <= 1e-12 * (1 + b.t) &&
           (a.normal - b.normal).length() < 1e-9;
    mismatches += ok ? 0 : 1;
    hits += hb ? 1 : 0;
  }
  w->pconfig->acceleration = AccelerationMethod::BVH;
  CHECK(mismatches == 0, what << ": " << mismatches << " of " << n_rays
                              << " rays differ from brute force");
  CHECK(hits > n_rays / 20, what << ": too few hits (" << hits << ") to be a meaningful test");
}

} // namespace

TEST(scene_bvh_matches_brute_force) {
  seed_thread_rng(77, 1);
  auto m = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
  auto w = th::make_world();
  for (int i = 0; i < 2000; ++i) {
    const point3 c = random_point(-3, 3);
    w->objects.push_back(std::make_shared<triangle>(c, c + random_point(-0.4, 0.4),
                                                    c + random_point(-0.4, 0.4), m));
  }
  for (int i = 0; i < 150; ++i)
    w->objects.push_back(std::make_shared<sphere>(random_point(-3, 3), random_double(0.05, 0.3), m));
  for (int i = 0; i < 40; ++i) // includes axis-aligned, zero-thickness boxes
    w->objects.push_back(std::make_shared<quad>(random_point(-3, 3), vec3(random_double(0.1, 1), 0, 0),
                                                vec3(0, 0, random_double(0.1, 1)), m));
  w->buildBVH();
  CHECK(w->bvh_root->getMaxDepth() < 64, "BVH depth stays bounded");
  compare_against_linear(w, 30000, "mixed scene");
}

TEST(bvh_handles_coincident_centroids) {
  // Hundreds of triangles sharing one centroid: SAH cannot split them.
  auto m = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
  auto w = th::make_world();
  seed_thread_rng(5, 9);
  for (int i = 0; i < 600; ++i) {
    const vec3 a = random_point(-1, 1), b = random_point(-1, 1);
    w->objects.push_back(std::make_shared<triangle>(a, b, -(a + b), m)); // centroid at 0
  }
  w->buildBVH();
  compare_against_linear(w, 5000, "coincident centroids");
}

TEST(mesh_bvh_matches_brute_force) {
  // Write a random triangle soup to an OBJ file and load it as a mesh.
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "raytracer_tests";
  fs::create_directories(dir);
  const fs::path obj = dir / "soup.obj";
  seed_thread_rng(31, 4);
  const int n_tri = 5000;
  {
    std::ofstream out(obj);
    for (int i = 0; i < n_tri; ++i) {
      const point3 c = random_point(-2, 2);
      for (int k = 0; k < 3; ++k) {
        const point3 v = c + random_point(-0.15, 0.15);
        out << "v " << v.x() << ' ' << v.y() << ' ' << v.z() << "\n";
      }
    }
    for (int i = 0; i < n_tri; ++i)
      out << "f " << 3 * i + 1 << ' ' << 3 * i + 2 << ' ' << 3 * i + 3 << "\n";
  }
  auto m = std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
  mesh msh(obj.string(), vec3(0, 0, 0), vec3(1, 1, 1), vec3(0, 0, 0), m);
  CHECK(msh.load(obj.string()), "mesh loads");
  CHECK(msh.getTriangleCount() == n_tri, "all triangles loaded");
  CHECK(msh.hasMeshBVH(), "mesh BVH built");

  int mismatches = 0, hits = 0;
  for (int i = 0; i < 20000; ++i) {
    const ray r = random_ray();
    hit_record a;
    const bool ha = msh.hit(r, 1e-9, INF, a);
    const bool any_a = msh.hit_any(r, 1e-9, INF);
    double best = INF;
    for (const triangle &t : msh.getTriangles()) {
      hit_record tmp;
      if (t.hit(r, 1e-9, best, tmp))
        best = tmp.t;
    }
    const bool hb = best < INF;
    bool ok = ha == hb && any_a == hb;
    if (ok && ha)
      ok = std::fabs(a.t - best) <= 1e-12 * (1 + best);
    mismatches += ok ? 0 : 1;
    hits += hb ? 1 : 0;
  }
  CHECK(mismatches == 0, mismatches << " mesh rays differ from brute force");
  CHECK(hits > 1000, "enough mesh hits for a meaningful test");
  std::error_code ec;
  fs::remove_all(dir, ec);
}
