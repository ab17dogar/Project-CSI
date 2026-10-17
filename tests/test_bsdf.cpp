// BSDF consistency: sample() / eval() / pdf() must describe one distribution.
#include "test_framework.h"

#include "engine/dielectric.h"
#include "engine/ggx_material.h"
#include "engine/isotropic.h"
#include "engine/lambertian.h"
#include "engine/metal.h"
#include "engine/microfacet.h"
#include "engine/pbr_material.h"
#include "engine/sampling.h"
#include "engine/sss_material.h"
#include "util/util.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace {

struct named_material {
  std::string name;
  std::shared_ptr<material> mat;
  bool chi2; // lobes broad enough for a binned goodness-of-fit test
};

std::vector<named_material> test_materials() {
  return {
      {"lambertian", std::make_shared<lambertian>(color(0.8, 0.5, 0.2)), true},
      {"pbr_dielectric_r0.3", std::make_shared<pbr_material>(color(0.9, 0.9, 0.9), 0.0f, 0.3f), true},
      {"pbr_dielectric_r0.6", std::make_shared<pbr_material>(color(0.5, 0.7, 0.2), 0.0f, 0.6f), true},
      {"pbr_dielectric_r1", std::make_shared<pbr_material>(color(1, 1, 1), 0.0f, 1.0f), true},
      {"pbr_metal_r0.4", std::make_shared<pbr_material>(color(0.95, 0.64, 0.54), 1.0f, 0.4f), true},
      {"pbr_half_metal_r0.7", std::make_shared<pbr_material>(color(0.8, 0.8, 0.3), 0.5f, 0.7f), true},
      {"pbr_rough_r0.1", std::make_shared<pbr_material>(color(0.9, 0.9, 0.9), 0.0f, 0.1f), false},
      {"pbr_smooth_dielectric", std::make_shared<pbr_material>(color(0.7, 0.2, 0.2), 0.0f, 0.0f), true},
      {"metal_fuzz0.5", std::make_shared<metal>(color(0.9, 0.9, 0.9), 0.5), true},
      {"metal_mirror", std::make_shared<metal>(color(0.9, 0.9, 0.9), 0.0), false},
      {"ggx_r0.5_m0.3", std::make_shared<ggx_material>(color(0.6, 0.6, 0.9), 0.5, 0.3), true},
      {"translucent", std::make_shared<sss_material>(color(0.8, 0.6, 0.5), color(0.9, 0.4, 0.3), 0.6f), true},
      {"isotropic_phase", std::make_shared<isotropic>(color(0.7, 0.7, 0.7)), true},
  };
}

hit_record shading_point(const material *m, const vec3 &n) {
  hit_record rec;
  rec.p = point3(0, 0, 0);
  rec.normal = unit_vector(n);
  rec.front_face = true;
  rec.mat_ptr = m;
  rec.u = rec.v = 0.25;
  return rec;
}

vec3 direction_with_cos(const vec3 &n, double cos_t, double phi) {
  const onb f(n);
  const double s = std::sqrt(std::max(0.0, 1 - cos_t * cos_t));
  return unit_vector(f.local(s * std::cos(phi), s * std::sin(phi), cos_t));
}

struct chi2_result {
  double z = 0;          // (X^2 - dof) / sqrt(2 dof)
  int dof = 0;
  double non_delta_fraction = 0;
};

// Pearson chi-square test of sample() against pdf() over the full sphere,
// with equal-area bins (uniform in cos(theta) and phi). Expected counts are
// integrated numerically. Bins below 5 expected samples are pooled. The test
// also checks normalization: pdf() must integrate to the probability of
// sample() returning a non-delta direction.
chi2_result chi2_test(const material &m, const hit_record &rec, const vec3 &wo,
                      int n_samples, uint64_t seed) {
  const int nt = 32, np = 64, sub = 10;
  const onb frame(rec.normal);
  std::vector<double> observed(nt * np, 0.0), expected(nt * np, 0.0);

  seed_thread_rng(seed, 99);
  int non_delta = 0;
  for (int i = 0; i < n_samples; ++i) {
    bsdf_sample bs;
    const double uc = random_double(), u1 = random_double(), u2 = random_double();
    if (!m.sample(rec, wo, uc, u1, u2, bs) || bs.is_delta)
      continue;
    ++non_delta;
    const vec3 l = frame.to_local(bs.wi);
    const int it = std::min(nt - 1, static_cast<int>((l.z() + 1) * 0.5 * nt));
    double phi = std::atan2(l.y(), l.x());
    if (phi < 0)
      phi += 2 * PI;
    const int ip = std::min(np - 1, static_cast<int>(phi / (2 * PI) * np));
    observed[it * np + ip] += 1;
  }

  const double d_omega = 4 * PI / (nt * np * sub * sub);
  for (int it = 0; it < nt; ++it) {
    for (int ip = 0; ip < np; ++ip) {
      double acc = 0;
      for (int a = 0; a < sub; ++a) {
        const double z = -1 + 2 * (it + (a + 0.5) / sub) / nt;
        const double s = std::sqrt(std::max(0.0, 1 - z * z));
        for (int b = 0; b < sub; ++b) {
          const double phi = 2 * PI * (ip + (b + 0.5) / sub) / np;
          const vec3 wi = frame.local(s * std::cos(phi), s * std::sin(phi), z);
          acc += m.pdf(rec, wo, wi);
        }
      }
      expected[it * np + ip] = acc * d_omega * n_samples;
    }
  }

  double stat = 0, pooled_o = 0, pooled_e = 0;
  int bins = 0;
  for (int k = 0; k < nt * np; ++k) {
    if (expected[k] < 5.0) {
      pooled_o += observed[k];
      pooled_e += expected[k];
      continue;
    }
    stat += (observed[k] - expected[k]) * (observed[k] - expected[k]) / expected[k];
    ++bins;
  }
  if (pooled_e >= 5.0) {
    stat += (pooled_o - pooled_e) * (pooled_o - pooled_e) / pooled_e;
    ++bins;
  } else if (pooled_o > 20) {
    // Samples landed where pdf() says there is (almost) no density.
    stat += pooled_o * pooled_o / std::max(pooled_e, 1.0);
    ++bins;
  }
  chi2_result r;
  r.dof = std::max(1, bins - 1);
  r.z = (stat - r.dof) / std::sqrt(2.0 * r.dof);
  r.non_delta_fraction = static_cast<double>(non_delta) / n_samples;
  return r;
}

// Deliberately broken: samples the uniform hemisphere but reports the
// cosine pdf. Used to show the chi-square test has the power to catch it.
class broken_lambertian : public lambertian {
public:
  broken_lambertian() : lambertian(color(0.5, 0.5, 0.5)) {}
  bool sample(const hit_record &rec, const vec3 &, double, double u1, double u2,
              bsdf_sample &out) const override {
    const double z = u1;
    const double r = std::sqrt(std::max(0.0, 1 - z * z));
    const vec3 l(r * std::cos(2 * PI * u2), r * std::sin(2 * PI * u2), z);
    out.wi = onb(rec.normal).local(l);
    out.pdf = z * INV_PI;
    out.weight = albedo;
    out.is_delta = false;
    return z > 0;
  }
};

} // namespace

TEST(bsdf_sample_weight_and_pdf_agree_with_eval) {
  const vec3 n = unit_vector(vec3(0.3, 1.0, -0.2));
  for (const auto &nm : test_materials()) {
    const material &m = *nm.mat;
    const hit_record rec = shading_point(&m, n);
    for (double cos_o : {0.95, 0.6, 0.15}) {
      const vec3 wo = direction_with_cos(n, cos_o, 1.3);
      seed_thread_rng(11, 22);
      int bad = 0;
      for (int i = 0; i < 20000; ++i) {
        bsdf_sample bs;
        if (!m.sample(rec, wo, random_double(), random_double(), random_double(), bs))
          continue;
        CHECK(std::fabs(bs.wi.length() - 1) < 1e-9, nm.name << ": unit wi");
        CHECK(is_finite(bs.weight) && min_component(bs.weight) >= 0,
              nm.name << ": weight finite and non-negative");
        if (bs.is_delta)
          continue;
        const double p = m.pdf(rec, wo, bs.wi);
        const double cos_i = m.is_phase_function() ? 1.0 : std::fabs(dot(bs.wi, n));
        const color expect = m.eval(rec, wo, bs.wi) * (cos_i / bs.pdf);
        const bool ok = std::fabs(p - bs.pdf) <= 1e-7 * bs.pdf &&
                        (expect - bs.weight).length() <= 1e-7 * (1 + bs.weight.length());
        bad += ok ? 0 : 1;
      }
      CHECK(bad == 0, nm.name << " cos_o=" << cos_o << ": " << bad
                              << " samples where pdf()/eval() disagree with sample()");
    }
  }
}

TEST(bsdf_sampling_matches_pdf_chi_square) {
  const vec3 n = unit_vector(vec3(0.0, 0.2, 1.0));
  for (const auto &nm : test_materials()) {
    if (!nm.chi2)
      continue;
    const hit_record rec = shading_point(nm.mat.get(), n);
    for (double cos_o : {0.9, 0.5, 0.2}) {
      const vec3 wo = direction_with_cos(n, cos_o, 0.4);
      const chi2_result r = chi2_test(*nm.mat, rec, wo, 400000, 7);
      CHECK(r.z < 6.0, nm.name << " cos_o=" << cos_o << ": chi-square z=" << r.z
                                << " (dof " << r.dof << ")");
    }
  }
}

TEST(chi_square_test_detects_a_broken_sampler) {
  broken_lambertian bad;
  const vec3 n(0, 0, 1);
  const hit_record rec = shading_point(&bad, n);
  const chi2_result r = chi2_test(bad, rec, vec3(0, 0, 1), 400000, 3);
  CHECK(r.z > 50.0, "a sampler that ignores its own pdf must fail (z=" << r.z << ")");
}

// Energy conservation: the directional albedo E[weight] never exceeds 1, and
// for a white Lambertian it is exactly 1.
TEST(bsdf_albedo_never_exceeds_one) {
  const vec3 n(0, 1, 0);
  std::vector<named_material> mats = test_materials();
  mats.push_back({"white_lambertian", std::make_shared<lambertian>(color(1, 1, 1)), true});
  mats.push_back({"white_pbr_smooth", std::make_shared<pbr_material>(color(1, 1, 1), 0.0f, 0.0f), true});
  mats.push_back({"white_pbr_r0.5", std::make_shared<pbr_material>(color(1, 1, 1), 0.0f, 0.5f), true});
  mats.push_back({"white_metal_r0.2", std::make_shared<pbr_material>(color(1, 1, 1), 1.0f, 0.2f), true});
  mats.push_back({"glass", std::make_shared<dielectric>(1.5), true});
  for (const auto &nm : mats) {
    const hit_record rec = shading_point(nm.mat.get(), n);
    for (double cos_o : {1.0, 0.7, 0.3, 0.05}) {
      const vec3 wo = direction_with_cos(n, cos_o, 0.0);
      seed_thread_rng(3, 4);
      const int count = 200000;
      double sum[3] = {0, 0, 0}, sum2[3] = {0, 0, 0};
      for (int i = 0; i < count; ++i) {
        bsdf_sample bs;
        color w(0, 0, 0);
        if (nm.mat->sample(rec, wo, random_double(), random_double(), random_double(), bs))
          w = bs.weight;
        // Transmission into a denser medium carries the 1/eta^2 radiance
        // factor; energy conservation is about the flux fraction, so undo it.
        if (bs.is_transmission)
          w = w * (bs.eta * bs.eta);
        for (int k = 0; k < 3; ++k) {
          sum[k] += w[k];
          sum2[k] += w[k] * w[k];
        }
      }
      for (int k = 0; k < 3; ++k) {
        const double mean = sum[k] / count;
        const double se = std::sqrt(std::max(0.0, sum2[k] / count - mean * mean) / count);
        CHECK(mean <= 1.0 + 4 * se + 1e-3,
              nm.name << " cos_o=" << cos_o << " albedo " << mean << " > 1");
        if (nm.name == "white_lambertian")
          CHECK_NEAR(mean, 1.0, 1e-12, "white Lambertian albedo");
        if (nm.name == "glass")
          CHECK_NEAR(mean, 1.0, 4 * se + 1e-9, "clear glass is lossless");
      }
    }
  }
}

TEST(bsdf_is_reciprocal) {
  const vec3 n(0, 0, 1);
  for (const auto &nm : test_materials()) {
    const hit_record rec = shading_point(nm.mat.get(), n);
    seed_thread_rng(8, 8);
    for (int i = 0; i < 2000; ++i) {
      const vec3 a = direction_with_cos(n, 0.02 + 0.97 * random_double(), 2 * PI * random_double());
      const vec3 b = direction_with_cos(n, 0.02 + 0.97 * random_double(), 2 * PI * random_double());
      const color fab = nm.mat->eval(rec, a, b);
      const color fba = nm.mat->eval(rec, b, a);
      CHECK((fab - fba).length() <= 1e-9 * (1 + fab.length()),
            nm.name << ": f(a,b) != f(b,a)");
      if (tf::current_test_failed())
        return;
    }
  }
}

TEST(dielectric_refraction_obeys_snell_and_fresnel) {
  dielectric glass(1.5);
  const vec3 n(0, 0, 1);
  for (bool entering : {true, false}) {
    hit_record rec = shading_point(&glass, n);
    rec.front_face = entering;
    const double eta = entering ? 1.5 : 1.0 / 1.5;
    for (double cos_o : {1.0, 0.8, 0.5, 0.2}) {
      const vec3 wo = direction_with_cos(n, cos_o, 0.7);
      const double F = microfacet::fresnel_dielectric(cos_o, eta);
      int reflected = 0, transmitted = 0;
      const int count = 100000;
      for (int i = 0; i < count; ++i) {
        bsdf_sample bs;
        CHECK(glass.sample(rec, wo, (i + 0.5) / count, 0.5, 0.5, bs), "glass always scatters");
        CHECK(bs.is_delta, "glass lobes are delta");
        if (bs.is_transmission) {
          ++transmitted;
          const double sin_o = std::sqrt(1 - cos_o * cos_o);
          const double sin_t = std::sqrt(std::max(0.0, 1 - dot(bs.wi, n) * dot(bs.wi, n)));
          CHECK_NEAR(sin_o, eta * sin_t, 1e-9, "Snell's law");
          CHECK(dot(bs.wi, n) < 0, "transmitted ray crosses the surface");
          CHECK_NEAR(bs.weight.x(), 1.0 / (eta * eta), 1e-12, "radiance scales by 1/eta^2");
          // Tangential components of wo and wt point in opposite directions.
          CHECK(dot(wo - dot(wo, n) * n, bs.wi - dot(bs.wi, n) * n) <= 1e-12,
                "refraction stays in the plane of incidence");
        } else {
          ++reflected;
          CHECK_NEAR((bs.wi - (2 * dot(wo, n) * n - wo)).length(), 0, 1e-12, "mirror direction");
        }
      }
      CHECK_NEAR(static_cast<double>(reflected) / count, F, 2.0 / count,
                 "reflection probability equals Fresnel");
      (void)transmitted;
    }
  }
}
