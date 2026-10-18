// Light sampling: sample_li() and pdf_li() must describe one distribution.
#include "test_framework.h"

#include "engine/emissive.h"
#include "engine/hdri_environment.h"
#include "engine/light.h"
#include "engine/quad.h"
#include "engine/sampling.h"
#include "engine/sphere.h"
#include "engine/sun.h"
#include "engine/triangle.h"
#include "engine/world.h"
#include "util/util.h"

#include <memory>
#include <vector>

namespace {

struct light_case {
  const char *name;
  std::shared_ptr<hittable> shape;
  point3 ref;
};

std::vector<light_case> light_cases(std::shared_ptr<material> e) {
  return {
      {"triangle", std::make_shared<triangle>(point3(-0.5, 1, -0.3), point3(0.7, 1.2, -0.2), point3(0.1, 0.9, 0.8), e),
       point3(0.05, 0, 0.1)},
      {"parallelogram", std::make_shared<quad>(point3(-0.5, 1, -0.5), vec3(1, 0, 0), vec3(0.2, 0.1, 1), e),
       point3(0.2, 0.1, -0.1)},
      {"sphere_outside", std::make_shared<sphere>(point3(0.3, 1.5, 0.2), 0.6, e), point3(0, 0, 0)},
      {"sphere_close", std::make_shared<sphere>(point3(0, 1.0, 0), 0.95, e), point3(0, 0, 0)},
      {"sphere_inside", std::make_shared<sphere>(point3(0, 0.2, 0), 1.0, e), point3(0.1, 0, 0.3)},
  };
}

} // namespace

// For every light shape: the pdf reported by sample_li equals pdf_li for the
// same direction, the sampled point lies on the shape, and pdf_li
// integrates to 1 over the sphere of directions (estimated by uniform
// sampling of directions + ray casting against the shape).
TEST(area_light_sampling_is_consistent) {
  auto e = std::make_shared<emissive>(color(2, 3, 4));
  for (auto &lc : light_cases(e)) {
    std::vector<std::shared_ptr<light>> lights;
    lc.shape->collect_lights(lights);
    CHECK(lights.size() == 1, lc.name << ": one light registered");
    if (lights.size() != 1)
      continue;
    const light &l = *lights[0];

    seed_thread_rng(1, 2);
    double inv_pdf_sum = 0;
    const int n = 50000;
    for (int i = 0; i < n; ++i) {
      light_sample ls;
      if (!l.sample_li(lc.ref, random_double(), random_double(), ls))
        continue;
      hit_record rec;
      const bool hit = lc.shape->hit(ray(lc.ref, ls.wi), 1e-9, INF, rec);
      CHECK(hit, lc.name << ": sampled direction must hit the light");
      if (!hit)
        break;
      // Sampled point is the first intersection along wi (for the sphere
      // from outside, the visible cap).
      CHECK_NEAR(rec.t, ls.dist, 1e-7 * (1 + ls.dist), lc.name << ": sample distance");
      CHECK(rec.area_light == &l, lc.name << ": hit reports its light");
      const double p = l.pdf_li(lc.ref, ls.wi, &rec);
      CHECK_NEAR(p, ls.pdf, 1e-7 * ls.pdf, lc.name << ": sample pdf == pdf_li");
      CHECK_NEAR((ls.li - color(2, 3, 4)).length(), 0, 1e-12, lc.name << ": radiance");
      inv_pdf_sum += 1.0 / ls.pdf;
      if (tf::current_test_failed())
        return;
    }
    // Uniform-direction estimates of the solid angle and of the pdf
    // integral (which must be 1).
    double omega = 0, pdf_integral = 0;
    const int m = 2000000;
    for (int i = 0; i < m; ++i) {
      const vec3 w = sampling::square_to_uniform_sphere((i + 0.5) / m, std::fmod(i * 0.6180339887498949, 1.0));
      hit_record rec;
      if (!lc.shape->hit(ray(lc.ref, w), 1e-9, INF, rec))
        continue;
      omega += 4 * PI / m;
      pdf_integral += l.pdf_li(lc.ref, w, &rec) * 4 * PI / m;
    }
    CHECK_NEAR(pdf_integral, 1.0, 0.01, lc.name << ": pdf_li integrates to 1");
    CHECK_NEAR(inv_pdf_sum / n, omega, 0.02 * omega, lc.name << ": E[1/pdf] = solid angle");
  }
}

TEST(environment_light_sampling_is_consistent) {
  // Synthetic HDR with a bright, small "sun" and a dim gradient.
  const int w = 64, h = 32;
  std::vector<float> px(w * h * 3);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      const bool sun = (i == 40 && j == 9);
      const float base = 0.2f + 0.8f * j / h;
      px[(j * w + i) * 3 + 0] = sun ? 500.f : base;
      px[(j * w + i) * 3 + 1] = sun ? 450.f : base * 0.9f;
      px[(j * w + i) * 3 + 2] = sun ? 400.f : base * 1.2f;
    }
  auto env = std::make_shared<hdri_environment>();
  env->set_pixels(w, h, px);
  env->rotation = 0.6;
  hdri_light l(env);

  seed_thread_rng(4, 4);
  const int n = 400000;
  double e_ratio[3] = {0, 0, 0};
  for (int i = 0; i < n; ++i) {
    light_sample ls;
    if (!l.sample_li(point3(0, 0, 0), random_double(), random_double(), ls))
      continue;
    CHECK_NEAR(l.pdf_li(point3(0, 0, 0), ls.wi, nullptr), ls.pdf, 1e-6 * ls.pdf,
               "env sample pdf == pdf_li");
    CHECK_NEAR((ls.li - l.le(ls.wi)).length(), 0, 1e-9, "env radiance consistent");
    for (int k = 0; k < 3; ++k)
      e_ratio[k] += ls.li[k] / ls.pdf / n;
    if (tf::current_test_failed())
      return;
  }
  // Uniform-direction reference for the integral of Le over the sphere.
  double ref[3] = {0, 0, 0}, pdf_int = 0;
  const int m = 4000000;
  for (int i = 0; i < m; ++i) {
    const vec3 d = sampling::square_to_uniform_sphere((i + 0.5) / m, std::fmod(i * 0.6180339887498949, 1.0));
    const color le = l.le(d);
    for (int k = 0; k < 3; ++k)
      ref[k] += le[k] * 4 * PI / m;
    pdf_int += l.pdf_li(point3(0, 0, 0), d, nullptr) * 4 * PI / m;
  }
  CHECK_NEAR(pdf_int, 1.0, 5e-3, "env pdf integrates to 1");
  for (int k = 0; k < 3; ++k)
    CHECK_NEAR(e_ratio[k], ref[k], 0.01 * ref[k], "importance-sampled integral of Le, channel " << k);
}

// The procedural sky: importance sampling by luminance must report the pdf
// that pdf_li gives, integrate to 1, and reproduce the integral of Le.
TEST(sky_light_sampling_is_consistent) {
  world w;
  w.psun = std::make_shared<sun>(vec3(0.3, 0.8, 0.2), color(1.0, 0.9, 0.8));
  w.skyColorTop = color(0.1, 0.3, 1.2);
  w.skyColorBottom = color(1.5, 1.4, 1.2);
  w.groundColor = color(0.2, 0.15, 0.1);
  sky_light l(&w);

  seed_thread_rng(5, 6);
  const int n = 400000;
  double e_ratio[3] = {0, 0, 0};
  for (int i = 0; i < n; ++i) {
    light_sample ls;
    if (!l.sample_li(point3(0, 0, 0), random_double(), random_double(), ls))
      continue;
    CHECK_NEAR(ls.wi.length(), 1.0, 1e-12, "sky sample is a unit direction");
    CHECK_NEAR(l.pdf_li(point3(0, 0, 0), ls.wi, nullptr), ls.pdf, 1e-9 * ls.pdf,
               "sky sample pdf == pdf_li");
    CHECK_NEAR((ls.li - l.le(ls.wi)).length(), 0, 1e-12, "sky radiance consistent");
    for (int k = 0; k < 3; ++k)
      e_ratio[k] += ls.li[k] / ls.pdf / n;
    if (tf::current_test_failed())
      return;
  }
  double ref[3] = {0, 0, 0}, pdf_int = 0;
  const int m = 4000000;
  for (int i = 0; i < m; ++i) {
    const vec3 d = sampling::square_to_uniform_sphere((i + 0.5) / m, std::fmod(i * 0.6180339887498949, 1.0));
    const color le = l.le(d);
    for (int k = 0; k < 3; ++k)
      ref[k] += le[k] * 4 * PI / m;
    pdf_int += l.pdf_li(point3(0, 0, 0), d, nullptr) * 4 * PI / m;
  }
  CHECK_NEAR(pdf_int, 1.0, 1e-3, "sky pdf integrates to 1");
  for (int k = 0; k < 3; ++k)
    CHECK_NEAR(e_ratio[k], ref[k], 0.005 * ref[k], "importance-sampled integral of sky Le, channel " << k);
}
