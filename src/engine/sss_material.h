#ifndef SSS_MATERIAL_H
#define SSS_MATERIAL_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "sampling.h"
#include "texture.h"
#include <algorithm>
#include <cmath>
#include <memory>

/**
 * @brief Diffuse translucent material (XML type "SSS").
 *
 * NOT true subsurface scattering. The previous implementation mixed ad-hoc
 * direction perturbations, had no pdf and leaked energy. That made it
 * incompatible with light sampling. This replacement is a physically valid,
 * reciprocal BSDF: Lambertian reflection with albedo
 *   R = surface_color * (1 - k)
 * plus Lambertian transmission to the opposite side with albedo
 *   T = scatter_color * k,
 * where k = clamp(scatter_distance, 0, 1) / 2. It conserves energy whenever
 * the input colors are <= 1.
 *
 * Proper volumetric random-walk subsurface scattering needs interior-medium
 * tracking in the integrator. That arrives with the participating-media work.
 */
class sss_material : public material {
public:
  bool transmits() const override { return true; }
  sss_material(const color &surface_color, const color &scatter_color,
               float scatter_distance = 0.5f, float roughness = 0.3f)
      : sss_material(make_shared<solid_color>(surface_color), scatter_color,
                     scatter_distance, roughness) {}

  sss_material(shared_ptr<texture> albedo_tex, const color &scatter_color,
               float scatter_distance = 0.5f, float roughness = 0.3f)
      : surface_albedo(albedo_tex), scatter_color(scatter_color),
        scatter_distance(scatter_distance), roughness(roughness) {
    k = 0.5 * std::clamp(static_cast<double>(scatter_distance), 0.0, 1.0);
  }

  color eval(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override {
    (void)wo;
    const double c = cos_theta(rec, wi);
    if (c > 0.0)
      return reflectance(rec) * INV_PI;
    if (c < 0.0)
      return transmittance() * INV_PI;
    return color(0, 0, 0);
  }

  double pdf(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override {
    (void)wo;
    const double c = cos_theta(rec, wi);
    const double pr = reflect_probability(rec);
    return (c > 0.0 ? pr : 1.0 - pr) * std::fabs(c) * INV_PI;
  }

  bool sample(const hit_record &rec, const vec3 &wo, double uc, double u1,
              double u2, bsdf_sample &out) const override {
    (void)wo;
    const double pr = reflect_probability(rec);
    vec3 local = sampling::square_to_cosine_hemisphere(u1, u2);
    if (local.z() <= 0.0)
      return false;
    const bool reflect = uc < pr;
    if (!reflect)
      local = vec3(local.x(), local.y(), -local.z());
    out.wi = unit_vector(onb(rec.normal).local(local));
    const double p = (reflect ? pr : 1.0 - pr) * std::fabs(local.z()) * INV_PI;
    if (!(p > 0.0))
      return false;
    out.pdf = p;
    const color f = (reflect ? reflectance(rec) : transmittance()) * INV_PI;
    out.weight = f * (std::fabs(local.z()) / p);
    out.is_delta = false;
    out.is_transmission = !reflect;
    out.eta = 1.0;
    return true;
  }

public:
  shared_ptr<texture> surface_albedo;
  color scatter_color;    // Color of light transmitted through the material
  float scatter_distance; // Controls the transmitted fraction
  float roughness;        // Unused by the diffuse model; kept for scene files

private:
  color reflectance(const hit_record &rec) const {
    return surface_albedo->value(rec.u, rec.v, rec.p) * (1.0 - k);
  }
  color transmittance() const { return scatter_color * k; }
  double reflect_probability(const hit_record &rec) const {
    const double r = luminance(reflectance(rec));
    const double t = luminance(transmittance());
    if (r + t <= 0.0)
      return 1.0;
    return std::clamp(r / (r + t), 0.05, 0.95);
  }

  double k;
};

#endif
