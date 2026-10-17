#include "pbr_material.h"

#include <algorithm>
#include <cmath>

#include "microfacet.h"
#include "sampling.h"

namespace {

// Directional albedo E(cos_theta, roughness) of a GGX lobe with Schlick
// Fresnel, F0 = 0.04, tabulated on a regular grid and linearly interpolated.
// Row 0 (roughness 0) is the smooth interface, whose albedo is exactly the
// Fresnel reflectance.
class specular_albedo_table {
public:
  static constexpr int kMu = 64;
  static constexpr int kRough = 32;

  specular_albedo_table() {
    const int strata = 32; // 32x32 stratified VNDF samples per cell
    for (int j = 0; j < kRough; ++j) {
      const double r = static_cast<double>(j) / (kRough - 1);
      const double alpha = r * r;
      for (int i = 0; i < kMu; ++i) {
        const double mu = std::max(1e-4, static_cast<double>(i) / (kMu - 1));
        if (j == 0) {
          E[j][i] = 0.04 + 0.96 * microfacet::schlick_weight(mu);
          continue;
        }
        const vec3 wo(std::sqrt(std::max(0.0, 1.0 - mu * mu)), 0.0, mu);
        double sum = 0.0;
        for (int a = 0; a < strata; ++a) {
          for (int b = 0; b < strata; ++b) {
            const double u1 = (a + 0.5) / strata;
            const double u2 = (b + 0.5) / strata;
            const vec3 h = microfacet::sample_vndf(wo, alpha, u1, u2);
            const vec3 wi = 2.0 * dot(wo, h) * h - wo;
            if (wi.z() <= 0.0)
              continue;
            const double F =
                0.04 + 0.96 * microfacet::schlick_weight(dot(wo, h));
            // f cos / pdf for VNDF-sampled reflection = F * G2 / G1(wo)
            sum += F * microfacet::ggx_G2(wo, wi, alpha) /
                   microfacet::ggx_G1(wo, alpha);
          }
        }
        E[j][i] = sum / (strata * strata);
      }
      // E_avg = 2 * integral of E(mu) mu dmu, taken over the same
      // interpolant that eval() uses so the diffuse normalization is exact.
      const int steps = 4096;
      double acc = 0.0;
      for (int k = 0; k < steps; ++k) {
        const double mu = (k + 0.5) / steps;
        acc += row(j, mu) * mu;
      }
      Eavg[j] = 2.0 * acc / steps;
    }
  }

  double lookup(double mu, double rough) const {
    const double y = std::clamp(rough, 0.0, 1.0) * (kRough - 1);
    const int j0 = std::min(static_cast<int>(y), kRough - 2);
    const double t = y - j0;
    return (1.0 - t) * row(j0, mu) + t * row(j0 + 1, mu);
  }

  double average(double rough) const {
    const double y = std::clamp(rough, 0.0, 1.0) * (kRough - 1);
    const int j0 = std::min(static_cast<int>(y), kRough - 2);
    const double t = y - j0;
    return (1.0 - t) * Eavg[j0] + t * Eavg[j0 + 1];
  }

private:
  double row(int j, double mu) const {
    const double x = std::clamp(mu, 0.0, 1.0) * (kMu - 1);
    const int i0 = std::min(static_cast<int>(x), kMu - 2);
    const double t = x - i0;
    return (1.0 - t) * E[j][i0] + t * E[j][i0 + 1];
  }

  double E[kRough][kMu];
  double Eavg[kRough];
};

const specular_albedo_table &albedo_table() {
  static const specular_albedo_table table;
  return table;
}

color clamp01(const color &c) {
  return color(std::clamp(c.x(), 0.0, 1.0), std::clamp(c.y(), 0.0, 1.0),
               std::clamp(c.z(), 0.0, 1.0));
}

constexpr double kSmoothRoughness = 0.01; // alpha = 1e-4

} // namespace

double pbr_material::dielectric_specular_albedo(double cos_theta,
                                                double roughness) {
  return albedo_table().lookup(cos_theta, roughness);
}

double pbr_material::dielectric_specular_average(double roughness) {
  return albedo_table().average(roughness);
}

pbr_material::pbr_material(shared_ptr<texture> albedo_tex, float metallic_in,
                           float roughness_in)
    : albedo(std::move(albedo_tex)),
      metallic(std::clamp(static_cast<double>(metallic_in), 0.0, 1.0)),
      roughness(std::clamp(static_cast<double>(roughness_in), 0.0, 1.0)) {
  smooth = roughness < kSmoothRoughness;
  alpha_ = std::max(roughness * roughness, 1e-4);
  albedo_table(); // build the table up front, outside the render loop
}

pbr_material::pbr_material(const color &c, float metallic_in,
                           float roughness_in)
    : pbr_material(make_shared<solid_color>(c), metallic_in, roughness_in) {}

pbr_material::lobe_setup pbr_material::setup(const hit_record &rec,
                                             double cos_o) const {
  lobe_setup s;
  s.base = clamp01(albedo->value(rec.u, rec.v, rec.p));
  s.f0 = lerp(metallic, color(0.04, 0.04, 0.04), s.base);
  const double spec_est = luminance(microfacet::fresnel_schlick(s.f0, cos_o));
  const double diff_est =
      (1.0 - metallic) * luminance(s.base) *
      (1.0 - dielectric_specular_albedo(cos_o, smooth ? 0.0 : roughness));
  if (diff_est <= 0.0) {
    s.spec_weight = 1.0;
  } else {
    s.spec_weight =
        std::clamp(spec_est / (spec_est + diff_est), 0.05, 0.95);
  }
  return s;
}

color pbr_material::diffuse_local(const lobe_setup &s, double cos_o,
                                  double cos_i) const {
  if (metallic >= 1.0)
    return color(0, 0, 0);
  const double r = smooth ? 0.0 : roughness;
  const double e_o = dielectric_specular_albedo(cos_o, r);
  const double e_i = dielectric_specular_albedo(cos_i, r);
  const double e_avg = dielectric_specular_average(r);
  return (1.0 - metallic) * s.base *
         (INV_PI * (1.0 - e_o) * (1.0 - e_i) / (1.0 - e_avg));
}

color pbr_material::eval_local(const lobe_setup &s, const vec3 &wo,
                               const vec3 &wi) const {
  if (wo.z() <= 0.0 || wi.z() <= 0.0)
    return color(0, 0, 0);
  color f = diffuse_local(s, wo.z(), wi.z());
  if (!smooth) {
    const vec3 h = unit_vector(wo + wi);
    const color F = microfacet::fresnel_schlick(s.f0, dot(wo, h));
    const double dg = microfacet::ggx_D(h, alpha_) *
                      microfacet::ggx_G2(wo, wi, alpha_) /
                      (4.0 * wo.z() * wi.z());
    f += F * dg;
  }
  return f;
}

double pbr_material::pdf_local(const lobe_setup &s, const vec3 &wo,
                               const vec3 &wi) const {
  if (wo.z() <= 0.0 || wi.z() <= 0.0)
    return 0.0;
  double p = (1.0 - s.spec_weight) * sampling::cosine_hemisphere_pdf(wi.z());
  if (!smooth)
    p += s.spec_weight * microfacet::vndf_reflection_pdf(wo, wi, alpha_);
  return p;
}

color pbr_material::eval(const hit_record &rec, const vec3 &wo,
                         const vec3 &wi) const {
  const onb frame(rec.normal);
  const vec3 wo_l = frame.to_local(wo);
  const vec3 wi_l = frame.to_local(wi);
  return eval_local(setup(rec, wo_l.z()), wo_l, wi_l);
}

double pbr_material::pdf(const hit_record &rec, const vec3 &wo,
                         const vec3 &wi) const {
  const onb frame(rec.normal);
  const vec3 wo_l = frame.to_local(wo);
  const vec3 wi_l = frame.to_local(wi);
  return pdf_local(setup(rec, wo_l.z()), wo_l, wi_l);
}

bool pbr_material::sample(const hit_record &rec, const vec3 &wo, double uc,
                          double u1, double u2, bsdf_sample &out) const {
  const onb frame(rec.normal);
  const vec3 wo_l = frame.to_local(wo);
  if (wo_l.z() <= 0.0)
    return false;
  const lobe_setup s = setup(rec, wo_l.z());

  vec3 wi_l;
  if (uc < s.spec_weight) {
    if (smooth) {
      // Perfect mirror: the delta in f cancels the delta in the pdf.
      wi_l = vec3(-wo_l.x(), -wo_l.y(), wo_l.z());
      out.wi = unit_vector(frame.local(wi_l));
      out.weight =
          microfacet::fresnel_schlick(s.f0, wo_l.z()) / s.spec_weight;
      out.pdf = 0.0;
      out.is_delta = true;
      out.is_transmission = false;
      out.eta = 1.0;
      return true;
    }
    const vec3 h = microfacet::sample_vndf(wo_l, alpha_, u1, u2);
    wi_l = 2.0 * dot(wo_l, h) * h - wo_l;
  } else {
    wi_l = sampling::square_to_cosine_hemisphere(u1, u2);
  }
  if (wi_l.z() <= 0.0)
    return false;

  const double p = pdf_local(s, wo_l, wi_l);
  if (!(p > 0.0))
    return false;
  out.wi = unit_vector(frame.local(wi_l));
  out.pdf = p;
  out.weight = eval_local(s, wo_l, wi_l) * (wi_l.z() / p);
  out.is_delta = false;
  out.is_transmission = false;
  out.eta = 1.0;
  return true;
}
