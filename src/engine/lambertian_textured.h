#ifndef LAMBERTIAN_TEXTURED_H
#define LAMBERTIAN_TEXTURED_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "sampling.h"
#include "texture.h"

/**
 * @brief Lambertian (diffuse) material with texture support
 *
 * This material uses a texture to define the albedo color at each point,
 * enabling detailed surface coloring from image textures.
 */
class lambertian_textured : public material {
public:
  lambertian_textured(shared_ptr<texture> a) : albedo(a) {}

  // Convenience constructor for solid color (wraps in solid_color texture)
  lambertian_textured(const color &a) : albedo(make_shared<solid_color>(a)) {}

  color eval(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override {
    (void)wo;
    if (cos_theta(rec, wi) <= 0.0)
      return color(0, 0, 0);
    return albedo->value(rec.u, rec.v, rec.p) * INV_PI;
  }

  double pdf(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override {
    (void)wo;
    return sampling::cosine_hemisphere_pdf(cos_theta(rec, wi));
  }

  bool sample(const hit_record &rec, const vec3 &wo, double uc, double u1,
              double u2, bsdf_sample &out) const override {
    (void)wo;
    (void)uc;
    const vec3 local = sampling::square_to_cosine_hemisphere(u1, u2);
    if (local.z() <= 0.0)
      return false;
    out.wi = unit_vector(onb(rec.normal).local(local));
    out.pdf = local.z() * INV_PI;
    out.weight = albedo->value(rec.u, rec.v, rec.p);
    out.is_delta = false;
    out.is_transmission = false;
    out.eta = 1.0;
    return true;
  }

public:
  shared_ptr<texture> albedo;
};

#endif
