// Random numbers, warps, distributions and microfacet normalization.
#include "test_framework.h"

#include "engine/microfacet.h"
#include "engine/onb.h"
#include "engine/sampling.h"
#include "util/util.h"
#include "util/vec3.h"

#include <vector>

TEST(rng_streams_are_reproducible_and_distinct) {
  seed_thread_rng(42, 7, 3);
  const double a0 = random_double(), a1 = random_double();
  seed_thread_rng(42, 7, 3);
  CHECK(random_double() == a0 && random_double() == a1,
        "same (pixel, sample, seed) must replay the same stream");
  seed_thread_rng(42, 8, 3);
  CHECK(random_double() != a0, "a different sample index must change the stream");
  seed_thread_rng(43, 7, 3);
  CHECK(random_double() != a0, "a different pixel must change the stream");

  // Uniformity: mean and variance of U[0,1).
  seed_thread_rng(1, 1);
  double s = 0, s2 = 0;
  const int n = 1000000;
  for (int i = 0; i < n; ++i) {
    const double u = random_double();
    CHECK(u >= 0.0 && u < 1.0, "sample outside [0,1)");
    s += u;
    s2 += u * u;
  }
  CHECK_NEAR(s / n, 0.5, 2e-3, "mean of U[0,1)");
  CHECK_NEAR(s2 / n - (s / n) * (s / n), 1.0 / 12.0, 1e-3, "variance of U[0,1)");
}

TEST(onb_is_orthonormal_for_all_normals) {
  seed_thread_rng(5, 5);
  for (int i = 0; i < 20000; ++i) {
    vec3 n = sampling::square_to_uniform_sphere(random_double(), random_double());
    if (i == 0)
      n = vec3(0, 0, -1); // the classic failure case
    if (i == 1)
      n = vec3(0, 0, 1);
    const onb f(n);
    CHECK_NEAR(f.u().length(), 1.0, 1e-12, "|u|");
    CHECK_NEAR(f.v().length(), 1.0, 1e-12, "|v|");
    CHECK_NEAR(dot(f.u(), f.v()), 0.0, 1e-12, "u.v");
    CHECK_NEAR(dot(f.u(), f.w()), 0.0, 1e-12, "u.w");
    CHECK_NEAR(dot(cross(f.u(), f.v()), f.w()), 1.0, 1e-12, "right-handed");
    const vec3 a(0.3, -0.5, 0.8);
    const vec3 back = f.local(f.to_local(a));
    CHECK_NEAR((back - a).length(), 0.0, 1e-12, "to_local/local roundtrip");
    if (tf::current_test_failed())
      break;
  }
}

// The integral of a warp's pdf over its domain must be 1, and the warp must
// produce samples distributed accordingly: E_warp[g] = integral of g * pdf.
TEST(warps_match_their_pdfs) {
  const int n = 400000;
  // Cosine hemisphere moments: E[z] = 2/3, E[z^2] = 1/2, E[x] = E[y] = 0,
  // E[x^2] = E[y^2] = 1/4.
  double e_z = 0, e_z2 = 0, e_x = 0, e_x2 = 0;
  for (int i = 0; i < n; ++i) {
    const double u1 = (i + 0.5) / n;
    const double u2 = std::fmod(i * 0.6180339887498949, 1.0);
    const vec3 w = sampling::square_to_cosine_hemisphere(u1, u2);
    CHECK_NEAR(w.length(), 1.0, 1e-9, "cosine warp returns unit vectors");
    e_z += w.z();
    e_z2 += w.z() * w.z();
    e_x += w.x();
    e_x2 += w.x() * w.x();
  }
  CHECK_NEAR(e_z / n, 2.0 / 3.0, 2e-3, "cosine hemisphere E[z]");
  CHECK_NEAR(e_z2 / n, 0.5, 2e-3, "cosine hemisphere E[z^2]");
  CHECK_NEAR(e_x / n, 0.0, 2e-3, "cosine hemisphere E[x]");
  CHECK_NEAR(e_x2 / n, 0.25, 2e-3, "cosine hemisphere E[x^2]");

  // Uniform cone: all samples inside the cone, E[1/pdf] = solid angle.
  for (double cos_max : {0.0, 0.5, 0.99, 1.0 - 1e-8}) {
    const double one_minus = 1.0 - cos_max;
    double inside = 0;
    for (int i = 0; i < 20000; ++i) {
      const vec3 w = sampling::square_to_uniform_cone(
          (i + 0.5) / 20000, std::fmod(i * 0.754877666, 1.0), one_minus);
      inside += (w.z() >= cos_max - 1e-12 && std::fabs(w.length() - 1) < 1e-9)
                    ? 1
                    : 0;
    }
    CHECK(inside == 20000, "cone samples must lie inside the cone (cos_max="
                               << cos_max << ")");
    CHECK_NEAR(1.0 / sampling::uniform_cone_pdf(one_minus),
               2 * PI * one_minus, 1e-12 * (1 + 2 * PI * one_minus),
               "cone solid angle");
  }

  // Triangle barycentrics: uniform over the triangle, so E[b] = 1/3 each.
  double b0s = 0, b1s = 0;
  for (int i = 0; i < n; ++i) {
    double b0, b1;
    sampling::square_to_triangle((i + 0.5) / n, std::fmod(i * 0.6180339887, 1.0),
                                 b0, b1);
    CHECK(b0 >= 0 && b1 >= 0 && b0 + b1 <= 1 + 1e-12, "barycentrics valid");
    b0s += b0;
    b1s += b1;
  }
  CHECK_NEAR(b0s / n, 1.0 / 3.0, 1e-3, "triangle E[b0]");
  CHECK_NEAR(b1s / n, 1.0 / 3.0, 1e-3, "triangle E[b1]");
}

TEST(distributions_sample_their_densities) {
  const std::vector<double> f = {0.0, 1.0, 3.0, 0.5, 0.0, 2.5};
  sampling::distribution1d d(f);
  double total = 0;
  for (double v : f)
    total += v;
  // Discrete pmf matches normalized values; histogram of samples matches.
  std::vector<double> hist(f.size(), 0.0);
  const int n = 600000;
  for (int i = 0; i < n; ++i) {
    double pmf;
    const size_t k = d.sample_discrete((i + 0.5) / n, pmf);
    CHECK_NEAR(pmf, f[k] / total, 1e-12, "discrete pmf of sampled index");
    hist[k] += 1;
  }
  for (size_t k = 0; k < f.size(); ++k) {
    CHECK_NEAR(d.discrete_pmf(k), f[k] / total, 1e-12, "discrete pmf " << k);
    CHECK_NEAR(hist[k] / n, f[k] / total, 1e-4, "discrete histogram " << k);
  }
  // Continuous density integrates to 1 and agrees with sample().
  double integral = 0;
  for (int i = 0; i < 6000; ++i)
    integral += d.pdf((i + 0.5) / 6000) / 6000;
  CHECK_NEAR(integral, 1.0, 1e-9, "1D continuous pdf integrates to 1");
  for (int i = 0; i < 1000; ++i) {
    double pdf;
    const double x = d.sample_continuous((i + 0.5) / 1000, pdf);
    CHECK_NEAR(pdf, d.pdf(x), 1e-12, "1D sample pdf == pdf(x)");
  }

  // 2D: pdf(u,v) integrates to 1 and matches sample().
  const int w = 7, h = 5;
  std::vector<double> img(w * h);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      img[j * w + i] = (i == 3 && j == 2) ? 50.0 : (i + 1) * (j % 3);
  sampling::distribution2d d2;
  d2.build(img, w, h);
  double integral2 = 0;
  const int g = 140;
  for (int j = 0; j < g; ++j)
    for (int i = 0; i < g; ++i)
      integral2 += d2.pdf((i + 0.5) / g, (j + 0.5) / g) / (g * g);
  CHECK_NEAR(integral2, 1.0, 1e-9, "2D pdf integrates to 1");
  for (int i = 0; i < 2000; ++i) {
    double u, v, pdf;
    d2.sample(std::fmod(i * 0.618034, 1.0), (i + 0.5) / 2000, u, v, pdf);
    CHECK_NEAR(pdf, d2.pdf(u, v), 1e-9 * (1 + pdf), "2D sample pdf == pdf(u,v)");
    CHECK(d2.pdf(u, v) > 0.0, "never samples a zero-density cell");
  }
}

// GGX: projected microfacet area integrates to 1, the VNDF integrates to 1,
// and Smith G1 equals the VNDF normalization constant (Heitz 2014, eq. 2).
TEST(ggx_distribution_is_normalized) {
  for (double alpha : {0.05, 0.2, 0.5, 1.0}) {
    const int nt = 20000, np = 64;
    double proj = 0;
    for (int it = 0; it < nt; ++it) {
      // cos(theta_h) parameterization concentrates points near the peak.
      const double c = (it + 0.5) / nt;
      const double s = std::sqrt(1 - c * c);
      for (int ip = 0; ip < np; ++ip) {
        const double phi = 2 * PI * (ip + 0.5) / np;
        const vec3 h(s * std::cos(phi), s * std::sin(phi), c);
        proj += microfacet::ggx_D(h, alpha) * c * (1.0 / nt) * (2 * PI / np);
      }
    }
    CHECK_NEAR(proj, 1.0, alpha < 0.1 ? 2e-2 : 2e-3,
               "integral of D(h) cos(theta_h) for alpha=" << alpha);

    for (double cos_o : {0.95, 0.5, 0.1}) {
      const vec3 wo(std::sqrt(1 - cos_o * cos_o), 0, cos_o);
      double vis = 0;
      for (int it = 0; it < nt; ++it) {
        const double c = (it + 0.5) / nt;
        const double s = std::sqrt(1 - c * c);
        for (int ip = 0; ip < np; ++ip) {
          const double phi = 2 * PI * (ip + 0.5) / np;
          const vec3 h(s * std::cos(phi), s * std::sin(phi), c);
          vis += std::max(0.0, dot(wo, h)) * microfacet::ggx_D(h, alpha) *
                 (1.0 / nt) * (2 * PI / np);
        }
      }
      // integral of max(0, wo.h) D(h) dh = cos_o / G1(wo)
      CHECK_NEAR(vis * microfacet::ggx_G1(wo, alpha) / cos_o, 1.0,
                 alpha < 0.1 ? 3e-2 : 3e-3,
                 "VNDF normalization alpha=" << alpha << " cos=" << cos_o);
    }
  }
}

TEST(dielectric_fresnel_limits) {
  // Normal incidence: ((eta - 1) / (eta + 1))^2, from either side.
  for (double eta : {1.33, 1.5, 2.4}) {
    const double r0 = std::pow((eta - 1) / (eta + 1), 2);
    CHECK_NEAR(microfacet::fresnel_dielectric(1.0, eta), r0, 1e-12, "R0 eta=" << eta);
    CHECK_NEAR(microfacet::fresnel_dielectric(1.0, 1.0 / eta), r0, 1e-12,
               "R0 from inside eta=" << eta);
    CHECK_NEAR(microfacet::fresnel_dielectric(0.0, eta), 1.0, 1e-12,
               "grazing incidence reflects everything");
    // Total internal reflection beyond the critical angle.
    const double sin_c = 1.0 / eta;
    const double cos_beyond = std::sqrt(1 - std::pow(sin_c * 1.01, 2));
    CHECK_NEAR(microfacet::fresnel_dielectric(cos_beyond, 1.0 / eta), 1.0, 1e-12,
               "total internal reflection");
  }
  CHECK_NEAR(microfacet::fresnel_dielectric(0.7, 1.0), 0.0, 1e-12,
             "index-matched interface does not reflect");
}
