// Gaussian field (3DGS capture): loading, spherical harmonics, the ray
// evaluation against closed forms and brute force, and the field as an
// emitter and occluder inside the path tracer.
#include "test_framework.h"
#include "test_helpers.h"

#include "engine/emissive.h"
#include "engine/factories/factory_methods.h"
#include "engine/gaussian_field.h"
#include "engine/lambertian.h"
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
