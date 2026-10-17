// End-to-end estimator tests against closed-form solutions. Each light
// transport strategy (MIS, BSDF-only, NEE-only) must converge to the same
// analytic value; a biased MIS weight or a wrong pdf shows up here.
#include "test_framework.h"
#include "test_helpers.h"

#include "engine/camera.h"
#include "engine/emissive.h"
#include "engine/hdri_environment.h"
#include "engine/lambertian.h"
#include "engine/pbr_material.h"
#include "engine/point_light.h"
#include "engine/quad.h"
#include "engine/render_runner.h"
#include "engine/sphere.h"
#include "engine/sun.h"
#include "engine/triangle.h"
#include "util/util.h"

#include <memory>

namespace {

// Point-to-rectangle form factor, receiver below one corner of an a x b
// rectangle in a parallel plane at distance c (Howell's catalog, C-11).
double form_factor_corner(double a, double b, double c) {
  const double X = a / c, Y = b / c;
  const double sx = std::sqrt(1 + X * X), sy = std::sqrt(1 + Y * Y);
  return (X / sx * std::atan(Y / sx) + Y / sy * std::atan(X / sy)) / (2 * PI);
}

const LightStrategy kStrategies[] = {LightStrategy::MIS, LightStrategy::BSDF,
                                     LightStrategy::NEE};

// Floor: large Lambertian quad in the y = 0 plane facing +y.
std::shared_ptr<quad> make_floor(std::shared_ptr<material> m) {
  return std::make_shared<quad>(point3(-50, 0, -50), vec3(0, 0, 100), vec3(100, 0, 0), m);
}

void check_estimate(const th::estimate &e, const color &expected, const char *what,
                    LightStrategy s) {
  for (int k = 0; k < 3; ++k) {
    const double tol = 4.5 * e.std_error[k] + 1e-9 + 1e-6 * expected[k];
    CHECK_NEAR(e.mean[k], expected[k], tol,
               what << " [" << th::strategy_name(s) << "] channel " << k);
  }
}

// A ray straight down onto the floor at the origin, from just above it.
const ray kDownRay(point3(0, 0.01, 0), vec3(0, -1, 0));

} // namespace

TEST(direct_light_from_rectangle_matches_form_factor) {
  const double rho = 0.6, Le = 3.0, side = 1.0, height = 1.0;
  auto w = th::make_world(1);
  w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
  // Light quad centred above the origin.
  w->objects.push_back(std::make_shared<quad>(point3(-side / 2, height, -side / 2), vec3(side, 0, 0),
                                              vec3(0, 0, side), std::make_shared<emissive>(color(Le, Le, Le))));
  w->prepare();
  const double F = 4 * form_factor_corner(side / 2, side / 2, height);
  const double expected = rho / PI * (PI * Le * F); // L = rho/pi * E
  for (LightStrategy s : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = s;
    const th::estimate e = th::estimate_radiance(*w, kDownRay, ps, 200000);
    check_estimate(e, color(expected, expected, expected), "rectangle light", s);
  }
}

TEST(direct_light_from_triangles_matches_form_factor) {
  // Same rectangle, split into two emissive triangles (how OBJ lights load).
  const double rho = 0.5, Le = 2.0, side = 0.8, height = 0.7;
  auto w = th::make_world(1);
  w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
  auto e = std::make_shared<emissive>(color(Le, Le, Le));
  const double s2 = side / 2;
  w->objects.push_back(std::make_shared<triangle>(point3(-s2, height, -s2), point3(s2, height, -s2),
                                                  point3(s2, height, s2), e));
  w->objects.push_back(std::make_shared<triangle>(point3(-s2, height, -s2), point3(s2, height, s2),
                                                  point3(-s2, height, s2), e));
  w->prepare();
  CHECK(w->lights.size() == 2, "two triangle lights");
  const double expected = rho * Le * 4 * form_factor_corner(s2, s2, height);
  for (LightStrategy s : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = s;
    check_estimate(th::estimate_radiance(*w, kDownRay, ps, 200000),
                   color(expected, expected, expected), "triangle lights", s);
  }
}

TEST(direct_light_from_sphere_matches_analytic) {
  // Irradiance from a uniformly emitting sphere fully above the horizon:
  // E = pi * Le * (R / d)^2.
  const double rho = 0.7, Le = 4.0, R = 0.3, d = 1.2;
  auto w = th::make_world(1);
  w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
  w->objects.push_back(std::make_shared<sphere>(point3(0, d, 0), R, std::make_shared<emissive>(color(Le, Le, Le))));
  w->prepare();
  const double expected = rho * Le * (R / d) * (R / d);
  for (LightStrategy s : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = s;
    check_estimate(th::estimate_radiance(*w, kDownRay, ps, 200000),
                   color(expected, expected, expected), "sphere light", s);
  }
}

TEST(point_and_directional_lights_match_analytic) {
  const double rho = 0.8;
  {
    // Point light at height h, offset x: E = I cos / r^2.
    const double I = 5.0, h = 1.5, x = 0.8;
    auto w = th::make_world(1);
    w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
    w->pointLights.push_back(std::make_shared<PointLight>(point3(x, h, 0), color(1, 1, 1), I));
    w->prepare();
    const double r2 = x * x + h * h;
    const double expected = rho / PI * I * (h / std::sqrt(r2)) / r2;
    for (LightStrategy s : kStrategies) {
      path_tracer_settings ps;
      ps.max_depth = 1;
      ps.strategy = s;
      const th::estimate e = th::estimate_radiance(*w, kDownRay, ps, 2000);
      check_estimate(e, color(expected, expected, expected), "point light", s);
      // Only floating-point rounding in the variance estimate remains.
      CHECK(e.std_error.x() < 1e-9 * expected, "a single delta light is noise-free");
    }
  }
  {
    // Sun at 60 degrees from zenith, irradiance E0: E = E0 cos(60).
    const double E0 = 2.0;
    auto w = th::make_world(1);
    w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
    w->psun = std::make_shared<sun>(vec3(std::sin(PI / 3), std::cos(PI / 3), 0), color(E0, E0, E0));
    w->skyColorTop = w->skyColorBottom = w->groundColor = color(0, 0, 0); // sun only
    w->prepare();
    const double expected = rho / PI * E0 * 0.5;
    for (LightStrategy s : kStrategies) {
      path_tracer_settings ps;
      ps.max_depth = 1;
      ps.strategy = s;
      check_estimate(th::estimate_radiance(*w, kDownRay, ps, 4000),
                     color(expected, expected, expected), "sun", s);
    }
  }
}

TEST(environment_lighting_matches_analytic) {
  // Uniform environment Le over the upper hemisphere: E = pi * Le, so the
  // floor's radiance is rho * Le. A bright patch tests importance sampling;
  // the expected value is integrated numerically from the same map.
  const int W = 32, H = 16;
  std::vector<float> px(W * H * 3);
  for (int j = 0; j < H; ++j)
    for (int i = 0; i < W; ++i)
      for (int k = 0; k < 3; ++k)
        px[(j * W + i) * 3 + k] = (i == 5 && j == 4) ? 200.0f : 1.0f + 0.1f * k;
  auto env = std::make_shared<hdri_environment>();
  env->set_pixels(W, H, px);

  const double rho = 0.5;
  auto w = th::make_world(1);
  w->objects.push_back(make_floor(std::make_shared<lambertian>(color(rho, rho, rho))));
  w->hdri = env;
  w->prepare();

  // Reference: E = integral over the upper hemisphere of Le cos.
  color E(0, 0, 0);
  const int nt = 2000, np = 4000;
  for (int it = 0; it < nt; ++it) {
    const double c = (it + 0.5) / nt; // cos(theta), uniform
    const double s = std::sqrt(1 - c * c);
    for (int ip = 0; ip < np; ++ip) {
      const double phi = 2 * PI * (ip + 0.5) / np;
      E += env->sample(vec3(s * std::cos(phi), c, s * std::sin(phi))) * c * (2 * PI / (nt * np));
    }
  }
  const color expected = rho / PI * E;
  for (LightStrategy s : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = s;
    check_estimate(th::estimate_radiance(*w, kDownRay, ps, 300000), expected, "environment", s);
  }
}

// White furnace: a non-absorbing scene inside a uniform environment of
// radiance 1 must render as exactly 1 everywhere, at any number of bounces,
// including the interreflections of a concave box. Exercises multi-bounce
// MIS, Russian roulette and the BVH together.
TEST(white_furnace_converges_to_environment) {
  const int W = 8, H = 4;
  std::vector<float> px(W * H * 3, 1.0f);
  auto make_scene = [&](std::shared_ptr<material> m) {
    auto w = th::make_world(256);
    auto env = std::make_shared<hdri_environment>();
    env->set_pixels(W, H, px);
    w->hdri = env;
    // Open box (no lid) with a sphere inside.
    w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(0, 0, 2), vec3(2, 0, 0), m));
    w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(2, 0, 0), vec3(0, 1.5, 0), m));
    w->objects.push_back(std::make_shared<quad>(point3(-1, 0, 1), vec3(0, 1.5, 0), vec3(2, 0, 0), m));
    w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(0, 1.5, 0), vec3(0, 0, 2), m));
    w->objects.push_back(std::make_shared<quad>(point3(1, 0, -1), vec3(0, 0, 2), vec3(0, 1.5, 0), m));
    w->objects.push_back(std::make_shared<sphere>(point3(0.2, 0.5, 0.1), 0.45, m));
    w->prepare();
    return w;
  };

  const ray rays[] = {ray(point3(0, 3, 0), vec3(0.1, -1, 0.05)),       // floor
                      ray(point3(0, 3, 0), vec3(0.2, -1, 0.1)),        // sphere
                      ray(point3(0, 1.2, 0), vec3(-1, -0.05, 0.3)),    // wall
                      ray(point3(-0.9, 0.05, -0.9), vec3(1, 0.3, 1))}; // corner
  {
    auto w = make_scene(std::make_shared<lambertian>(color(1, 1, 1)));
    for (LightStrategy s : kStrategies) {
      path_tracer_settings ps;
      ps.max_depth = 256;
      ps.rr_depth = 2;
      ps.strategy = s;
      for (const ray &r : rays) {
        const th::estimate e = th::estimate_radiance(*w, r, ps, 40000);
        check_estimate(e, color(1, 1, 1), "white Lambertian furnace", s);
      }
    }
  }
  {
    // Energy-conserving microfacet model: never brighter than the furnace.
    auto w = make_scene(std::make_shared<pbr_material>(color(1, 1, 1), 0.0f, 0.4f));
    path_tracer_settings ps;
    ps.max_depth = 256;
    ps.rr_depth = 2;
    for (const ray &r : rays) {
      const th::estimate e = th::estimate_radiance(*w, r, ps, 40000);
      CHECK(e.mean.x() <= 1.0 + 4.5 * e.std_error.x(),
            "PBR furnace gains energy: " << e.mean.x() << " +- " << e.std_error.x());
      CHECK(e.mean.x() > 0.85, "PBR furnace loses too much energy: " << e.mean.x());
    }
  }
}

// Multi-bounce agreement: in a closed Cornell-like box with a ceiling light,
// all three strategies estimate the same pixel value (no closed form, but
// any MIS weighting error makes them disagree).
TEST(strategies_agree_on_indirect_lighting) {
  auto w = th::make_world(6);
  auto white = std::make_shared<lambertian>(color(0.73, 0.73, 0.73));
  auto red = std::make_shared<lambertian>(color(0.65, 0.05, 0.05));
  auto glossy = std::make_shared<pbr_material>(color(0.9, 0.9, 0.9), 0.0f, 0.35f);
  w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(0, 0, 2), vec3(2, 0, 0), glossy));
  w->objects.push_back(std::make_shared<quad>(point3(-1, 2, -1), vec3(2, 0, 0), vec3(0, 0, 2), white));
  w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(2, 0, 0), vec3(0, 2, 0), white));
  w->objects.push_back(std::make_shared<quad>(point3(-1, 0, -1), vec3(0, 2, 0), vec3(0, 0, 2), red));
  w->objects.push_back(std::make_shared<quad>(point3(1, 0, -1), vec3(0, 0, 2), vec3(0, 2, 0), white));
  w->objects.push_back(std::make_shared<quad>(point3(-0.25, 1.999, -0.25), vec3(0.5, 0, 0), vec3(0, 0, 0.5),
                                              std::make_shared<emissive>(color(12, 12, 12))));
  w->objects.push_back(std::make_shared<sphere>(point3(0.3, 0.4, -0.2), 0.4, white));
  w->prepare();

  const ray r(point3(-0.6, 0.8, 0.9), vec3(0.3, -0.5, -1));
  th::estimate est[3];
  for (int i = 0; i < 3; ++i) {
    path_tracer_settings ps;
    ps.max_depth = 6;
    ps.strategy = kStrategies[i];
    est[i] = th::estimate_radiance(*w, r, ps, 300000, 17);
  }
  for (int i = 1; i < 3; ++i)
    for (int k = 0; k < 3; ++k) {
      const double sigma = std::sqrt(est[0].std_error[k] * est[0].std_error[k] +
                                     est[i].std_error[k] * est[i].std_error[k]);
      CHECK_NEAR(est[i].mean[k], est[0].mean[k], 4.5 * sigma,
                 th::strategy_name(kStrategies[i]) << " vs mis, channel " << k);
    }
  // MIS should not be noisier than the worse of the two strategies.
  CHECK(est[0].std_error.x() <= std::max(est[1].std_error.x(), est[2].std_error.x()),
        "MIS variance exceeds both strategies");
}

// Images must not depend on the number of threads.
TEST(renders_are_deterministic_across_thread_counts) {
  auto w = th::make_world(4);
  w->pconfig->IMAGE_WIDTH = 24;
  w->pconfig->IMAGE_HEIGHT = 16;
  w->pconfig->SAMPLES_PER_PIXEL = 3;
  w->pconfig->seed = 1234;
  w->pcamera = std::make_shared<camera>(point3(0, 1, 3), point3(0, 0.5, 0), vec3(0, 1, 0), 45.0, 1.5);
  w->objects.push_back(make_floor(std::make_shared<pbr_material>(color(0.8, 0.8, 0.8), 0.0f, 0.5f)));
  w->objects.push_back(std::make_shared<sphere>(point3(0, 0.5, 0), 0.5, std::make_shared<lambertian>(color(0.2, 0.5, 0.8))));
  w->objects.push_back(std::make_shared<quad>(point3(-1, 2, -1), vec3(2, 0, 0), vec3(0, 0, 2),
                                              std::make_shared<emissive>(color(4, 4, 4))));
  std::vector<color> a, b;
  render::RenderSceneToBitmap(*w, a, 1, 8, false);
  render::RenderSceneToBitmap(*w, b, 3, 5, false);
  bool same = a.size() == b.size();
  for (size_t i = 0; same && i < a.size(); ++i)
    same = a[i].x() == b[i].x() && a[i].y() == b[i].y() && a[i].z() == b[i].z();
  CHECK(same, "1-thread and 3-thread renders differ");
  double total = 0;
  for (const auto &c : a)
    total += c.x();
  CHECK(total > 0, "render is not black");
  CHECK(render::InvalidSampleCount() == 0, "no NaN/Inf samples");
}

// A one-sided emitter lights only the side its normal faces, consistently
// for light sampling and for BSDF-sampled hits (and so for MIS).
TEST(one_sided_emitters_light_only_their_front) {
  const double rho = 0.5, Le = 3.0, side = 1.0, height = 1.0;
  auto light_mat = std::make_shared<emissive>(color(Le, Le, Le));
  light_mat->two_sided = false;
  auto w = th::make_world(1);
  auto white = std::make_shared<lambertian>(color(rho, rho, rho));
  w->objects.push_back(make_floor(white));
  // Ceiling plane above the light, facing down.
  w->objects.push_back(std::make_shared<quad>(point3(-50, 2 * height, -50), vec3(100, 0, 0),
                                              vec3(0, 0, 100), white));
  // Normal cross(u, v) = (0, -1, 0): the light faces the floor.
  w->objects.push_back(std::make_shared<quad>(point3(-side / 2, height, -side / 2), vec3(side, 0, 0),
                                              vec3(0, 0, side), light_mat));
  w->prepare();
  const double expected_floor = rho * Le * 4 * form_factor_corner(side / 2, side / 2, height);
  const ray up_to_ceiling(point3(0, 2 * height - 0.01, 0), vec3(0, 1, 0));
  for (LightStrategy s : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = s;
    check_estimate(th::estimate_radiance(*w, kDownRay, ps, 100000),
                   color(expected_floor, expected_floor, expected_floor), "front side lit", s);
    const th::estimate back = th::estimate_radiance(*w, up_to_ceiling, ps, 20000);
    CHECK(back.mean.x() == 0.0, "the back side emits nothing [" << th::strategy_name(s) << "]");
  }
}
