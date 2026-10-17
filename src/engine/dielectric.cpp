#include "dielectric.h"
#include "../util/vec3.h"
#include "hittable.h"
#include "microfacet.h"

bool dielectric::sample(const hit_record &rec, const vec3 &wo, double uc,
                        double u1, double u2, bsdf_sample &out) const {
  (void)u1;
  (void)u2;
  const vec3 &n = rec.normal; // faces wo
  // Relative IOR for light crossing from wo's side to the other side.
  const double eta = rec.front_face ? ir : 1.0 / ir;
  const double cos_o = dot(wo, n);
  const double F = microfacet::fresnel_dielectric(cos_o, eta);

  out.pdf = 0.0;
  out.is_delta = true;

  if (uc < F) {
    out.wi = unit_vector(2.0 * cos_o * n - wo);
    out.weight = color(1, 1, 1); // F / P(reflect) with P(reflect) = F
    out.is_transmission = false;
    out.eta = 1.0;
    return true;
  }

  const double sin2_t = std::max(0.0, 1.0 - cos_o * cos_o) / (eta * eta);
  if (sin2_t >= 1.0)
    return false; // unreachable: F = 1 under total internal reflection
  const double cos_t = std::sqrt(1.0 - sin2_t);
  out.wi = unit_vector(-wo / eta + (cos_o / eta - cos_t) * n);
  out.weight = tint / (eta * eta); // (1 - F) / P(refract) * 1/eta^2
  out.is_transmission = true;
  out.eta = eta;
  return true;
}
