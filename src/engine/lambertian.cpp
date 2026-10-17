
#include "lambertian.h"
#include "sampling.h"

color lambertian::eval(const hit_record &rec, const vec3 &wo,
                       const vec3 &wi) const {
  (void)wo;
  return cos_theta(rec, wi) > 0.0 ? albedo * INV_PI : color(0, 0, 0);
}

double lambertian::pdf(const hit_record &rec, const vec3 &wo,
                       const vec3 &wi) const {
  (void)wo;
  return sampling::cosine_hemisphere_pdf(cos_theta(rec, wi));
}

bool lambertian::sample(const hit_record &rec, const vec3 &wo, double uc,
                        double u1, double u2, bsdf_sample &out) const {
  (void)wo;
  (void)uc;
  const vec3 local = sampling::square_to_cosine_hemisphere(u1, u2);
  if (local.z() <= 0.0)
    return false;
  out.wi = unit_vector(onb(rec.normal).local(local));
  out.pdf = local.z() * INV_PI;
  out.weight = albedo; // (albedo/pi) * cos / (cos/pi)
  out.is_delta = false;
  out.is_transmission = false;
  out.eta = 1.0;
  return true;
}
