// Gaussian field (3DGS capture): loading, spherical harmonics, the ray
// evaluation against closed forms and brute force, and the field as an
// emitter and occluder inside the path tracer.
#include "test_framework.h"
#include "test_helpers.h"

#include "engine/emissive.h"
#include "engine/factories/factory_methods.h"
#include "engine/gaussian_field.h"
#include "engine/lambertian.h"
#include "engine/light_sampler.h"
#include "engine/point_light.h"
#include "engine/quad.h"
#include "engine/sampling.h"
#include "util/util.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace {

constexpr double kC0 = 0.28209479177387814;
constexpr double kC1 = 0.4886025119029199;

// Degree-0 SH coefficients that give constant radiance c (= C0 f + 0.5).
std::vector<float> constant_sh(const color &c) {
  return {static_cast<float>((c.x() - 0.5) / kC0), static_cast<float>((c.y() - 0.5) / kC0),
          static_cast<float>((c.z() - 0.5) / kC0)};
}

const double kIdentity[4] = {1, 0, 0, 0};

// Rotation of a unit quaternion (w, x, y, z) applied to v (reference formula,
// independent of the field's matrix code).
vec3 rotate(const double q[4], const vec3 &v) {
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  const vec3 u(q[1] / n, q[2] / n, q[3] / n);
  const double w = q[0] / n;
  return 2.0 * dot(u, v) * u + (w * w - dot(u, u)) * v + 2.0 * w * cross(u, v);
}

// Brute-force reference of gaussian_field::trace for a field without a
// transform: every Gaussian, maximum response along the ray, sorted.
void brute_force(const gaussian_field &f, const ray &r, double t_min, double t_max,
                 color &L, double &T) {
  struct h {
    double t, a;
    int i;
  };
  std::vector<h> hits;
  const auto &gs = f.gaussians();
  for (size_t i = 0; i < gs.size(); ++i) {
    const auto &g = gs[i];
    double o[3], d[3];
    for (int k = 0; k < 3; ++k) {
      o[k] = d[k] = 0;
      for (int j = 0; j < 3; ++j) {
        o[k] += g.whiten[k * 3 + j] * (r.origin()[j] - g.mean[j]);
        d[k] += g.whiten[k * 3 + j] * r.direction()[j];
      }
    }
    const double dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
    const double t = -(o[0] * d[0] + o[1] * d[1] + o[2] * d[2]) / dd;
    if (!(t > t_min && t < t_max))
      continue;
    double m2 = 0;
    for (int k = 0; k < 3; ++k)
      m2 += (o[k] + t * d[k]) * (o[k] + t * d[k]);
    const double a = g.opacity * std::exp(-0.5 * m2);
    if (a >= gaussian_field::kMinAlpha)
      hits.push_back({t, a, static_cast<int>(i)});
  }
  std::sort(hits.begin(), hits.end(), [](const h &x, const h &y) { return x.t < y.t; });
  L = color(0, 0, 0);
  T = 1.0;
  for (const h &x : hits) {
    L += T * x.a * gaussian_field::eval_sh(f.sh_degree(), gs[static_cast<size_t>(x.i)].sh, r.direction());
    T *= 1.0 - x.a;
  }
}

// A 3DGS-layout binary PLY: x y z nx ny nz f_dc_0..2 f_rest_0..44 opacity
// scale_0..2 rot_0..3, raw (pre-activation) values.
struct raw_gaussian {
  float xyz[3];
  float dc[3];
  float rest[45];
  float opacity_logit;
  float log_scale[3];
  float rot[4];
};

void write_3dgs_ply(const std::string &path, const std::vector<raw_gaussian> &gs) {
  std::ofstream f(path, std::ios::binary);
  f << "ply\nformat binary_little_endian 1.0\nelement vertex " << gs.size() << "\n";
  for (const char *p : {"x", "y", "z", "nx", "ny", "nz", "f_dc_0", "f_dc_1", "f_dc_2"})
    f << "property float " << p << "\n";
  for (int i = 0; i < 45; ++i)
    f << "property float f_rest_" << i << "\n";
  f << "property float opacity\n";
  for (const char *p : {"scale_0", "scale_1", "scale_2", "rot_0", "rot_1", "rot_2", "rot_3"})
    f << "property float " << p << "\n";
  f << "end_header\n";
  for (const auto &g : gs) {
    const float normal[3] = {0, 0, 0};
    f.write(reinterpret_cast<const char *>(g.xyz), 12);
    f.write(reinterpret_cast<const char *>(normal), 12);
    f.write(reinterpret_cast<const char *>(g.dc), 12);
    f.write(reinterpret_cast<const char *>(g.rest), 45 * 4);
    f.write(reinterpret_cast<const char *>(&g.opacity_logit), 4);
    f.write(reinterpret_cast<const char *>(g.log_scale), 12);
    f.write(reinterpret_cast<const char *>(g.rot), 16);
  }
}

std::shared_ptr<quad> floor_quad(std::shared_ptr<material> m) {
  return std::make_shared<quad>(point3(-50, 0, -50), vec3(0, 0, 100), vec3(100, 0, 0), m);
}

const LightStrategy kStrategies[] = {LightStrategy::MIS, LightStrategy::BSDF, LightStrategy::NEE};
const ray kDown(point3(0, 0.01, 0), vec3(0, -1, 0));

} // namespace

// The real SH basis must be orthonormal over the sphere, with the signs and
// ordering of the 3DGS reference (spot values below).
TEST(sh_basis_is_orthonormal_and_matches_reference) {
  // Basis function k in channel 0, read back through eval_sh with a small
  // coefficient so the clamp at 0 never engages.
  const double a = 0.1;
  auto basis = [&](int k, const vec3 &d) {
    float sh[48] = {};
    sh[k * 3] = static_cast<float>(a);
    return (gaussian_field::eval_sh(3, sh, d).x() - 0.5) / a;
  };
  const int nt = 300, np = 600;
  double gram[16][16] = {};
  for (int it = 0; it < nt; ++it) {
    const double z = -1 + 2 * (it + 0.5) / nt;
    const double s = std::sqrt(1 - z * z);
    for (int ip = 0; ip < np; ++ip) {
      const double phi = 2 * PI * (ip + 0.5) / np;
      const vec3 d(s * std::cos(phi), s * std::sin(phi), z);
      double y[16];
      for (int k = 0; k < 16; ++k)
        y[k] = basis(k, d);
      for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j)
          gram[i][j] += y[i] * y[j] * (4 * PI / (nt * np));
    }
  }
  for (int i = 0; i < 16; ++i)
    for (int j = 0; j < 16; ++j)
      CHECK_NEAR(gram[i][j], i == j ? 1.0 : 0.0, 2e-3, "SH Gram matrix (" << i << "," << j << ")");
  // Spot values: Y1 = -C1 y, Y2 = C1 z, Y3 = -C1 x.
  const vec3 d = unit_vector(vec3(0.3, -0.5, 0.8));
  CHECK_NEAR(basis(0, d), kC0, 1e-6, "Y0");
  CHECK_NEAR(basis(1, d), -kC1 * d.y(), 1e-6, "Y1");
  CHECK_NEAR(basis(2, d), kC1 * d.z(), 1e-6, "Y2");
  CHECK_NEAR(basis(3, d), -kC1 * d.x(), 1e-6, "Y3");
  CHECK_NEAR(basis(6, d), 0.31539156525252005 * (2 * d.z() * d.z() - d.x() * d.x() - d.y() * d.y()),
             1e-6, "Y6");
  // Radiance is SH + 0.5, clamped at 0.
  float neg[48] = {};
  neg[0] = neg[1] = neg[2] = -10.0f;
  CHECK(gaussian_field::eval_sh(0, neg, d).length() == 0.0, "negative radiance clamps to 0");
}

// A file in the reference layout: activations, f_rest ordering, quaternion
// normalization, dropping of invisible Gaussians, and loading through a scene.
TEST(gaussian_ply_loads_like_the_reference) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "rt_gaussian_ply_test";
  fs::create_directories(dir);
  std::vector<raw_gaussian> raw(3);
  for (auto &g : raw)
    std::memset(&g, 0, sizeof g);
  // 0: identity rotation given unnormalized, opacity sigmoid(2).
  raw[0] = raw_gaussian{{0.5f, 1.0f, -2.0f}, {0.1f, 0.2f, 0.3f}, {}, 2.0f,
                        {std::log(0.2f), std::log(0.5f), std::log(0.1f)}, {2, 0, 0, 0}};
  for (int c = 0; c < 3; ++c)
    for (int k = 0; k < 15; ++k)
      raw[0].rest[c * 15 + k] = 0.01f * (100 * c + k + 1); // channel-major
  // 1: rotated 90 degrees about z.
  raw[1] = raw_gaussian{{-1, 0, 0}, {0, 0, 0}, {}, 8.0f,
                        {std::log(0.3f), std::log(0.1f), std::log(0.1f)},
                        {static_cast<float>(std::sqrt(0.5)), 0, 0, static_cast<float>(std::sqrt(0.5))}};
  // 2: opacity sigmoid(-10) < 1/255: never visible, dropped.
  raw[2] = raw_gaussian{{0, 0, 0}, {0, 0, 0}, {}, -10.0f, {0, 0, 0}, {1, 0, 0, 0}};
  const std::string ply = (dir / "g.ply").string();
  write_3dgs_ply(ply, raw);

  gaussian_field f;
  std::string error;
  CHECK(f.load_ply(ply, &error), "load: " << error);
  CHECK(f.size() == 2, "invisible Gaussian dropped, got " << f.size());
  CHECK(f.sh_degree() == 3, "degree from 45 f_rest");
  if (f.size() != 2)
    return;
  const auto &g0 = f.gaussians()[0];
  CHECK_NEAR(g0.opacity, 1 / (1 + std::exp(-2.0)), 1e-6, "sigmoid opacity");
  CHECK_NEAR(f.gaussians()[1].opacity, gaussian_field::kMaxOpacity, 1e-6, "opacity clamped to 0.99");
  for (int c = 0; c < 3; ++c) {
    CHECK_NEAR(g0.sh[c], raw[0].dc[c], 1e-7, "f_dc channel " << c);
    for (int k = 0; k < 15; ++k)
      CHECK_NEAR(g0.sh[(k + 1) * 3 + c], 0.01f * (100 * c + k + 1), 1e-7,
                 "f_rest channel " << c << " coefficient " << k + 1);
  }
  // Whitening: one standard deviation along each rotated axis maps to unit length.
  const double q1[4] = {std::sqrt(0.5), 0, 0, std::sqrt(0.5)};
  const double s1[3] = {0.3, 0.1, 0.1};
  const auto &g1 = f.gaussians()[1];
  for (int axis = 0; axis < 3; ++axis) {
    vec3 e(0, 0, 0);
    e[axis] = s1[axis];
    const vec3 p = rotate(q1, e); // offset from the mean
    double l2 = 0;
    for (int k = 0; k < 3; ++k) {
      double v = 0;
      for (int j = 0; j < 3; ++j)
        v += g1.whiten[k * 3 + j] * p[j];
      l2 += v * v;
    }
    CHECK_NEAR(l2, 1.0, 1e-6, "one sigma along rotated axis " << axis);
  }

  // The same file through a scene, placed by a transform.
  const std::string xml = (dir / "scene.xml").string();
  {
    std::ofstream s(xml);
    s << "<?xml version=\"1.0\"?>\n<ItemContainer>\n"
         "<Config><Width value=\"8\"/><Aspect_ratio value=\"1\"/><Samples_Per_Pixel value=\"1\"/>"
         "<Max_Depth value=\"2\"/></Config>\n"
         "<Camera><Look_From x=\"0\" y=\"0\" z=\"5\"/><Look_at x=\"0\" y=\"0\" z=\"0\"/>"
         "<Up x=\"0\" y=\"1\" z=\"0\"/><FOV angle=\"40\"/></Camera>\n"
         "<Lights><Sun><Direction x=\"0\" y=\"1\" z=\"0\"/><Intensity value=\"0\"/>"
         "<Color r=\"1\" g=\"1\" b=\"1\"/></Sun></Lights>\n"
         "<Materials/>\n<Objects>\n"
         "<GaussianField file=\"g.ply\"><Position x=\"10\" y=\"0\" z=\"0\"/><Scale value=\"2\"/>"
         "<Rotation x=\"0\" y=\"90\" z=\"0\"/></GaussianField>\n"
         "</Objects>\n</ItemContainer>\n";
  }
  auto w = LoadScene(xml);
  CHECK(w && w->field && w->field->size() == 2, "scene loads the field");
  if (w && w->field) {
    // Local (0.5, 1, -2) -> Ry(90): (-2, 1, -0.5) -> * 2 + (10, 0, 0) = (6, 2, -1).
    const aabb b = w->field->world_bounds();
    CHECK(b.min().x() < 6 && b.max().x() > 6 && b.min().y() < 2 && b.max().y() > 2 &&
              b.min().z() < -1 && b.max().z() > -1,
          "transformed bounds contain the transformed mean");
  }
  fs::remove_all(dir);
}

// One anisotropic, rotated Gaussian: alpha along a ray through its mean is
// the opacity, and off-axis it falls as exp(-b^2 / 2 s^2).
TEST(gaussian_alpha_matches_closed_form) {
  const double q[4] = {0.9, 0.2, -0.3, 0.25};
  const vec3 scale(0.2, 0.5, 0.1);
  const double o = 0.8;
  const color c(1.5, 0.7, 2.0);
  const vec3 mean(0.3, -0.2, 1.0);
  gaussian_field f;
  CHECK(f.add(mean, scale, q, o, constant_sh(c).data(), 0), "add");
  f.build();
  const vec3 ax = rotate(q, vec3(1, 0, 0)), ay = rotate(q, vec3(0, 1, 0));
  for (double b : {0.0, 0.2, 0.5, 0.9}) {
    // Along the first axis, offset b along the second (scale 0.5).
    const ray r(mean - 3.0 * ax + b * ay, ax);
    color L;
    double T;
    f.trace(r, 0.0, INF, L, T);
    const double alpha = o * std::exp(-0.5 * b * b / (0.5 * 0.5));
    CHECK_NEAR(T, 1 - alpha, 1e-6, "transmittance at offset " << b);
    CHECK_NEAR((L - alpha * c).length(), 0.0, 1e-5, "emission at offset " << b);
    CHECK_NEAR(f.transmittance(r, 0.0, INF), T, 1e-12, "transmittance() == trace()");
  }
  // The maximum response (t = 3) outside the segment: no contribution.
  color L;
  double T;
  f.trace(ray(mean - 3.0 * ax, ax), 0.0, 2.9, L, T);
  CHECK(T == 1.0 && L.length() == 0.0, "segment ending before the maximum response");
}

// Many Gaussians: BVH traversal + sorting equals the brute-force sum, and a
// transformed field equals the untransformed one traced in local coordinates.
TEST(gaussian_field_matches_brute_force_and_transforms) {
  seed_thread_rng(7, 7);
  gaussian_field f, moved;
  for (int i = 0; i < 300; ++i) {
    const vec3 mean(random_double(-1, 1), random_double(-1, 1), random_double(-1, 1));
    const vec3 scale(random_double(0.02, 0.2), random_double(0.02, 0.2), random_double(0.02, 0.2));
    const double q[4] = {random_double(-1, 1), random_double(-1, 1), random_double(-1, 1),
                         random_double(-1, 1)};
    float sh[12];
    for (float &v : sh)
      v = static_cast<float>(random_double(-0.5, 0.5));
    const double op = random_double(0.05, 1.0);
    f.add(mean, scale, q, op, sh, 1);
    moved.add(mean, scale, q, op, sh, 1);
  }
  f.build();
  const vec3 pos(3, -1, 2), rot(20, -35, 60);
  const double s = 1.7;
  moved.set_transform(pos, s, rot);
  moved.build();
  // Local -> world for rays: x_w = pos + s R x_l, R = Rx Ry Rz.
  auto R = [&](const vec3 &v) {
    const double a = degrees_to_radians(rot.x()), b = degrees_to_radians(rot.y()),
                 c = degrees_to_radians(rot.z());
    const vec3 z(v.x() * std::cos(c) - v.y() * std::sin(c), v.x() * std::sin(c) + v.y() * std::cos(c), v.z());
    const vec3 y(z.x() * std::cos(b) + z.z() * std::sin(b), z.y(), -z.x() * std::sin(b) + z.z() * std::cos(b));
    return vec3(y.x(), y.y() * std::cos(a) - y.z() * std::sin(a), y.y() * std::sin(a) + y.z() * std::cos(a));
  };
  int nonempty = 0;
  for (int i = 0; i < 400; ++i) {
    const point3 o(random_double(-2, 2), random_double(-2, 2), -3);
    const vec3 d = unit_vector(vec3(random_double(-0.4, 0.4), random_double(-0.4, 0.4), 1));
    const double t_max = random_double(2, 8);
    const ray r(o, d);
    color L, Lb;
    double T, Tb;
    f.trace(r, 0.0, t_max, L, T);
    brute_force(f, r, 0.0, t_max, Lb, Tb);
    nonempty += T < 1.0;
    CHECK_NEAR(T, Tb, 1e-12, "BVH transmittance, ray " << i);
    CHECK_NEAR((L - Lb).length(), 0.0, 1e-10, "BVH emission, ray " << i);
    // World ray of the moved field; its parameter t is the same.
    const ray rw(pos + s * R(o), s * R(d));
    color Lw;
    double Tw;
    moved.trace(rw, 0.0, t_max, Lw, Tw);
    CHECK_NEAR(Tw, T, 1e-6, "transformed transmittance, ray " << i);
    CHECK_NEAR((Lw - L).length(), 0.0, 1e-5 * (1 + L.length()), "transformed emission, ray " << i);
    if (tf::current_test_failed())
      return;
  }
  // About half the rays meet this sparse field: the comparison is not vacuous.
  CHECK(nonempty > 150, "rays meeting Gaussians: " << nonempty);
}

// The field as the only light: a floor under one isotropic Gaussian of
// constant radiance c. From the floor point, alpha(theta) = o exp(-sin^2
// theta |mu|^2 / 2 s^2) toward the upper hemisphere (t > 0 there), so
// L = rho / pi * c * 2 pi integral of alpha cos sin dtheta, with the same
// 1/255 cut-off. Every strategy must agree: nothing light-samples the field.
TEST(gaussian_field_lights_a_floor) {
  const double rho = 0.5, h = 1.0, s = 0.25, o = 0.9;
  const color c(2, 3, 4);
  auto w = th::make_world(1);
  w->objects.push_back(floor_quad(std::make_shared<lambertian>(color(rho, rho, rho))));
  auto f = std::make_shared<gaussian_field>();
  f->add(vec3(0, h, 0), vec3(s, s, s), kIdentity, o, constant_sh(c).data(), 0);
  f->build();
  w->field = f;
  w->prepare();
  double integral = 0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double theta = (i + 0.5) / n * (PI / 2);
    const double alpha = o * std::exp(-0.5 * std::sin(theta) * std::sin(theta) * h * h / (s * s));
    if (alpha >= gaussian_field::kMinAlpha)
      integral += alpha * std::cos(theta) * std::sin(theta) * (PI / 2) / n;
  }
  const color expected = rho / PI * c * (2 * PI * integral);
  for (LightStrategy st : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = st;
    const th::estimate e = th::estimate_radiance(*w, kDown, ps, 200000);
    for (int k = 0; k < 3; ++k)
      CHECK_NEAR(e.mean[k], expected[k], 4.5 * e.std_error[k] + 1e-9,
                 "floor under a Gaussian [" << th::strategy_name(st) << "] channel " << k);
  }
}

// The field as an occluder: a black Gaussian centred on the segment between
// the floor point and a point light passes exactly 1 - opacity of its light.
TEST(gaussian_field_shadows_a_point_light) {
  const double rho = 0.5, I = 3.0, o = 0.6;
  auto w = th::make_world(1);
  w->objects.push_back(floor_quad(std::make_shared<lambertian>(color(rho, rho, rho))));
  w->pointLights.push_back(std::make_shared<PointLight>(point3(0, 2, 0), color(1, 1, 1), I));
  auto f = std::make_shared<gaussian_field>();
  f->add(vec3(0, 1, 0), vec3(0.1, 0.1, 0.1), kIdentity, o, constant_sh(color(0, 0, 0)).data(), 0);
  f->build();
  w->field = f;
  w->prepare();
  const double expected = rho / PI * I / 4.0 * (1 - o);
  for (LightStrategy st : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = st;
    const th::estimate e = th::estimate_radiance(*w, kDown, ps, 200);
    CHECK_NEAR(e.mean.x(), expected, 1e-6 * expected,
               "point light through a Gaussian [" << th::strategy_name(st) << "]");
  }
}

// ... and on the path itself: an emitter seen through a black Gaussian of
// opacity o appears 1 - o as bright.
TEST(gaussian_field_attenuates_what_is_behind_it) {
  const double o = 0.7;
  const color le(2, 4, 6);
  auto w = th::make_world(1);
  w->objects.push_back(std::make_shared<quad>(point3(-5, -5, -2), vec3(10, 0, 0), vec3(0, 10, 0),
                                              std::make_shared<emissive>(le)));
  auto f = std::make_shared<gaussian_field>();
  f->add(vec3(0, 0, -1), vec3(0.2, 0.2, 0.2), kIdentity, o, constant_sh(color(0, 0, 0)).data(), 0);
  f->build();
  w->field = f;
  w->prepare();
  for (LightStrategy st : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = st;
    const th::estimate e = th::estimate_radiance(*w, ray(point3(0, 0, 0), vec3(0, 0, -1)), ps, 50);
    CHECK_NEAR((e.mean - (1 - o) * le).length(), 0.0, 1e-6,
               "emitter behind a Gaussian [" << th::strategy_name(st) << "]");
  }
}

// Light sampling of one Gaussian: the direction density integrates to 1, the
// sampler reports it, and sampled directions follow it (chi-square on a
// gnomonic grid around the direction to the mean; only directions the
// sampler accepts, those whose maximum response lies ahead and above the
// 1/255 cut-off, are counted on both sides).
TEST(gaussian_emission_sampling_matches_pdf) {
  const double q[4] = {0.8, 0.3, -0.4, 0.2};
  const vec3 mean(0.2, 1.0, -0.1);
  gaussian_field f;
  f.add(mean, vec3(0.25, 0.08, 0.15), q, 0.9, constant_sh(color(1, 2, 3)).data(), 0);
  f.build();
  CHECK(f.power() > 0, "power");
  for (const point3 x : {point3(0, 0, 0), mean + vec3(0.02, -0.03, 0.01)}) {
    // Normalization over the sphere (fine grid: the density is peaked).
    const int nt = 1500, np = 3000;
    double integral = 0;
    for (int it = 0; it < nt; ++it) {
      const double th = PI * (it + 0.5) / nt;
      for (int ip = 0; ip < np; ++ip) {
        const double ph = 2 * PI * (ip + 0.5) / np;
        const vec3 w(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
        integral += f.emission_pdf(0, x, w) * std::sin(th) * (PI / nt) * (2 * PI / np);
      }
    }
    CHECK_NEAR(integral, 1.0, 2e-3, "direction pdf integrates to 1, x = " << x);
  }

  const point3 x(0, 0, 0);
  const vec3 z = unit_vector(mean - x);
  const vec3 e1 = unit_vector(cross(std::abs(z.x()) < 0.9 ? vec3(1, 0, 0) : vec3(0, 1, 0), z));
  const vec3 e2 = cross(z, e1);
  const int nb = 32, sub = 6;
  const double R = 1.0; // half-width of the grid in tan(angle): +-45 degrees
  std::vector<double> observed(nb * nb + 1, 0.0), expected(nb * nb + 1, 0.0);
  auto bin_of = [&](const vec3 &w) {
    const double c = dot(w, z);
    if (c <= 0)
      return nb * nb;
    const double u = dot(w, e1) / c, v = dot(w, e2) / c;
    if (std::abs(u) >= R || std::abs(v) >= R)
      return nb * nb;
    return static_cast<int>((u + R) / (2 * R) * nb) * nb + static_cast<int>((v + R) / (2 * R) * nb);
  };
  seed_thread_rng(11, 3);
  const int n = 300000;
  for (int i = 0; i < n; ++i) {
    light_sample ls;
    const double u[4] = {random_double(), random_double(), random_double(), random_double()};
    if (!f.sample_emission(x, random_double(), u, ls))
      continue;
    CHECK_NEAR(ls.pdf, f.emission_pdf(ls.field_index, x, ls.wi), 1e-9 * ls.pdf, "reported pdf");
    CHECK_NEAR(ls.wi.length(), 1.0, 1e-12, "unit direction");
    observed[static_cast<size_t>(bin_of(ls.wi))] += 1;
    if (tf::current_test_failed())
      return;
  }
  // Expected counts: n * integral over the bin of p * [accepted], with
  // d omega = du dv / (1 + u^2 + v^2)^(3/2) on the gnomonic plane.
  const double h = 2 * R / (nb * sub);
  for (int a = 0; a < nb * sub; ++a)
    for (int b = 0; b < nb * sub; ++b) {
      const double u = -R + (a + 0.5) * h, v = -R + (b + 0.5) * h;
      const vec3 w = unit_vector(z + u * e1 + v * e2);
      color L;
      double T;
      f.trace(ray(x, w), 0.0, INF, L, T);
      if (T >= 1.0)
        continue; // rejected by the sampler: no response ahead of x
      const double m = f.emission_pdf(0, x, w) * h * h / std::pow(1 + u * u + v * v, 1.5);
      expected[static_cast<size_t>((a / sub) * nb + b / sub)] += n * m;
    }
  double stat = 0;
  int bins = 0;
  for (int k = 0; k < nb * nb; ++k)
    if (expected[k] >= 5.0) {
      stat += (observed[k] - expected[k]) * (observed[k] - expected[k]) / expected[k];
      ++bins;
    }
  const int dof = bins - 1;
  const double zscore = (stat - dof) / std::sqrt(2.0 * dof);
  CHECK(bins > 200, "enough populated bins: " << bins);
  CHECK(zscore < 6.0, "chi-square z = " << zscore << " over " << bins << " bins");
  // Nearly all accepted samples fall on the grid.
  CHECK(observed[nb * nb] < 0.01 * n, "samples off the grid: " << observed[nb * nb]);
}

// The field lighting a floor through many overlapping Gaussians: view-
// dependent colour, a small bright "lamp" Gaussian, and one of zero power
// (never light-sampled) whose degree-1 lobe still emits, plus a point light
// seen through the field (so the field is picked with probability < 1).
// Reference: the hemisphere integral of the field's own trace() and the
// point light's irradiance times the field's transmittance. Every strategy
// must agree, which tests the per-Gaussian MIS weights, the transmittance in
// front of a light-sampled Gaussian and the zero-power case; MIS must also
// beat BSDF sampling on noise.
TEST(gaussian_field_light_sampling_is_unbiased) {
  seed_thread_rng(21, 5);
  auto f = std::make_shared<gaussian_field>();
  for (int i = 0; i < 12; ++i) {
    const vec3 mean(random_double(-0.8, 0.8), random_double(0.5, 1.5), random_double(-0.8, 0.8));
    const vec3 scale(random_double(0.05, 0.25), random_double(0.05, 0.25), random_double(0.05, 0.25));
    const double q[4] = {random_double(-1, 1), random_double(-1, 1), random_double(-1, 1),
                         random_double(-1, 1)};
    float sh[12];
    const color dc(random_double(0.2, 3), random_double(0.2, 3), random_double(0.2, 3));
    const std::vector<float> c0 = constant_sh(dc);
    for (int k = 0; k < 3; ++k)
      sh[k] = c0[static_cast<size_t>(k)];
    for (int k = 3; k < 12; ++k)
      sh[k] = static_cast<float>(random_double(-0.3, 0.3));
    f->add(mean, scale, q, random_double(0.3, 0.95), sh, 1);
  }
  f->add(vec3(0.3, 1.2, 0.2), vec3(0.04, 0.04, 0.04), kIdentity, 0.95,
         constant_sh(color(40, 35, 30)).data(), 0); // the lamp
  float lobe[12] = {};
  for (int k = 0; k < 3; ++k)
    lobe[k] = static_cast<float>(-0.5 / kC0); // DC radiance 0: zero power
  // Y1 = -C1 y, evaluated with the viewing direction: a negative coefficient
  // makes the Gaussian bright to viewers looking up, i.e. from the floor.
  lobe[3 + 1] = -2.0f; // green
  f->add(vec3(-0.3, 0.8, -0.2), vec3(0.2, 0.2, 0.2), kIdentity, 0.8, lobe, 1);
  f->build();

  const double rho = 0.6;
  auto w = th::make_world(1);
  w->objects.push_back(floor_quad(std::make_shared<lambertian>(color(rho, rho, rho))));
  const point3 light_pos(0.5, 2.5, -0.4);
  const double I = 30.0;
  w->pointLights.push_back(std::make_shared<PointLight>(light_pos, color(1, 1, 1), I));
  w->field = f;
  w->prepare();
  CHECK(w->fieldLight != nullptr, "field registered as a light");
  CHECK(w->lights.size() == 2 && w->lightSampler->pmf(point3(0, 0, 0), vec3(0, 1, 0), w->fieldLight) < 0.9,
        "the field competes with the point light");

  // Reference: rho / pi * integral over the upper hemisphere of L cos.
  const point3 x(0, 0, 0);
  const int nt = 1200, np = 2400;
  color E(0, 0, 0);
  for (int it = 0; it < nt; ++it) {
    const double th = (PI / 2) * (it + 0.5) / nt;
    const double weight = std::cos(th) * std::sin(th) * (PI / 2 / nt) * (2 * PI / np);
    for (int ip = 0; ip < np; ++ip) {
      const double ph = 2 * PI * (ip + 0.5) / np;
      const vec3 dir(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
      color L;
      double T;
      f->trace(ray(x, dir), 0.0, INF, L, T);
      E += weight * L;
    }
  }
  // The point light, through the field.
  const vec3 to_light = light_pos - x;
  const double r2 = to_light.length_squared();
  const double T_light = f->transmittance(ray(x, unit_vector(to_light)), 0.0, std::sqrt(r2));
  CHECK(T_light < 0.999, "the field shadows the point light (T = " << T_light << ")");
  E += color(1, 1, 1) * (I * (to_light.y() / std::sqrt(r2)) / r2 * T_light);
  const color expected = rho / PI * E;
  double se[3] = {0, 0, 0};
  for (auto sel : {gaussian_field::selection::power, gaussian_field::selection::tree}) {
    f->set_selection(sel);
    const char *sel_name = sel == gaussian_field::selection::tree ? "tree" : "power";
    for (LightStrategy st : kStrategies) {
      path_tracer_settings ps;
      ps.max_depth = 1;
      ps.strategy = st;
      const th::estimate e = th::estimate_radiance(*w, kDown, ps, 300000);
      for (int k = 0; k < 3; ++k)
        CHECK_NEAR(e.mean[k], expected[k], 4.5 * e.std_error[k] + 2e-4 * expected[k],
                   "many Gaussians, " << sel_name << " [" << th::strategy_name(st) << "] channel " << k);
      if (sel == gaussian_field::selection::power)
        se[static_cast<int>(st == LightStrategy::BSDF ? 1 : st == LightStrategy::NEE ? 2 : 0)] =
            e.std_error.x();
    }
  }
  // Picking the point light adds the same selection noise to both, so the
  // gap is smaller than with the field alone (0.59x measured, variance 3x).
  CHECK(se[0] < 0.75 * se[1], "MIS standard error " << se[0] << " vs BSDF " << se[1]);
}

// The light hierarchy: at any shading point the selection probabilities of
// all Gaussians sum to 1, sampled choices follow them, and nearby emitters
// are preferred over equally bright distant ones.
TEST(gaussian_light_tree_selection_is_consistent) {
  seed_thread_rng(31, 1);
  gaussian_field f;
  for (int i = 0; i < 500; ++i) {
    const vec3 mean(random_double(-5, 5), random_double(0, 3), random_double(-5, 5));
    const double s = random_double(0.02, 0.3);
    f.add(mean, vec3(s, s * random_double(0.5, 1), s * random_double(0.5, 1)), kIdentity,
          random_double(0.2, 0.95),
          constant_sh(color(random_double(0.1, 20), random_double(0.1, 20), random_double(0.1, 20))).data(), 0);
  }
  f.set_transform(vec3(0.5, -0.2, 1.0), 1.3, vec3(10, 30, -5));
  f.build();
  f.set_selection(gaussian_field::selection::tree);
  for (const point3 x : {point3(0, 0, 0), point3(4, 1, -3), point3(-20, 5, 2)}) {
    double sum = 0;
    std::vector<double> pmf(f.size());
    for (size_t i = 0; i < f.size(); ++i)
      sum += pmf[i] = f.select_pmf(static_cast<int>(i), x);
    CHECK_NEAR(sum, 1.0, 1e-9, "tree pmf sums to 1 at " << x);
    std::vector<double> count(f.size(), 0.0);
    const int n = 400000;
    for (int k = 0; k < n; ++k) {
      double p;
      const int i = f.select_gaussian(x, random_double(), p);
      CHECK(i >= 0, "a Gaussian is selected");
      if (i < 0)
        return;
      CHECK_NEAR(p, pmf[static_cast<size_t>(i)], 1e-9 * pmf[static_cast<size_t>(i)], "reported pmf");
      count[static_cast<size_t>(i)] += 1;
      if (tf::current_test_failed())
        return;
    }
    double stat = 0, pooled_o = 0, pooled_e = 0;
    int bins = 0;
    for (size_t i = 0; i < f.size(); ++i) {
      const double e = n * pmf[i];
      if (e < 5) {
        pooled_o += count[i];
        pooled_e += e;
        continue;
      }
      stat += (count[i] - e) * (count[i] - e) / e;
      ++bins;
    }
    if (pooled_e >= 5) {
      stat += (pooled_o - pooled_e) * (pooled_o - pooled_e) / pooled_e;
      ++bins;
    }
    const double z = (stat - (bins - 1)) / std::sqrt(2.0 * (bins - 1));
    CHECK(z < 6.0, "tree selection chi-square z = " << z << " over " << bins << " bins at " << x);
  }
  // Two identical lights, one near x and one far: the near one dominates.
  gaussian_field two;
  two.add(vec3(0, 1, 0), vec3(0.05, 0.05, 0.05), kIdentity, 0.9, constant_sh(color(5, 5, 5)).data(), 0);
  two.add(vec3(30, 1, 0), vec3(0.05, 0.05, 0.05), kIdentity, 0.9, constant_sh(color(5, 5, 5)).data(), 0);
  two.build();
  two.set_selection(gaussian_field::selection::tree);
  CHECK(two.select_pmf(0, point3(0, 0, 0)) > 0.99, "near light preferred: " << two.select_pmf(0, point3(0, 0, 0)));
  two.set_selection(gaussian_field::selection::power);
  CHECK_NEAR(two.select_pmf(0, point3(0, 0, 0)), 0.5, 1e-12, "power selection ignores distance");
}

// MIS weights on BSDF rays must use the same selection probability as light
// sampling. Here the tree and power selection disagree strongly: two equally
// powerful Gaussians, one small and near, one large and far, the far one
// seen at a grazing angle where BSDF and light densities are comparable. A
// floor lit by them against quadrature, tree selection, every strategy.
TEST(gaussian_light_tree_mis_weights_match_its_sampling) {
  auto f = std::make_shared<gaussian_field>();
  f->add(vec3(0.2, 1.0, 0.1), vec3(0.15, 0.15, 0.15), kIdentity, 0.9, constant_sh(color(8, 8, 8)).data(), 0);
  // Same power: Phi scales with s^2 L, so 10x the size at 1/100 the radiance.
  f->add(vec3(6.0, 2.0, 0.0), vec3(1.5, 1.5, 1.5), kIdentity, 0.9, constant_sh(color(0.08, 0.08, 0.08)).data(), 0);
  f->build();
  f->set_selection(gaussian_field::selection::tree);
  const double rho = 0.5;
  auto w = th::make_world(1);
  w->objects.push_back(floor_quad(std::make_shared<lambertian>(color(rho, rho, rho))));
  w->field = f;
  w->prepare();
  const point3 x(0, 0, 0);
  CHECK(f->select_pmf(1, x) < 0.1, "tree favours the near Gaussian: far pmf " << f->select_pmf(1, x));
  const int nt = 1500, np = 3000;
  double E = 0;
  for (int it = 0; it < nt; ++it) {
    const double th = (PI / 2) * (it + 0.5) / nt;
    const double weight = std::cos(th) * std::sin(th) * (PI / 2 / nt) * (2 * PI / np);
    for (int ip = 0; ip < np; ++ip) {
      const double ph = 2 * PI * (ip + 0.5) / np;
      color L;
      double T;
      f->trace(ray(x, vec3(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph))), 0.0, INF, L, T);
      E += weight * L.x();
    }
  }
  const double expected = rho / PI * E;
  for (LightStrategy st : kStrategies) {
    path_tracer_settings ps;
    ps.max_depth = 1;
    ps.strategy = st;
    const th::estimate e = th::estimate_radiance(*w, kDown, ps, 300000);
    CHECK_NEAR(e.mean.x(), expected, 4.5 * e.std_error.x() + 2e-4 * expected,
               "near and far Gaussian, tree [" << th::strategy_name(st) << "]");
  }
}
