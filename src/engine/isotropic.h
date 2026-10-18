#ifndef ISOTROPIC_H
#define ISOTROPIC_H

#include "material.h"
#include "sampling.h"
#include "texture.h"

/**
 * @brief Isotropic phase function for participating media.
 *
 * Used inside constant_medium. `albedo` is the single-scattering albedo
 * sigma_s / sigma_t, and the phase function is 1/(4 pi). There is no cosine
 * term at a volume scattering event.
 */
class isotropic : public material {
public:
  isotropic(color c) : albedo(make_shared<solid_color>(c)) {}
  isotropic(shared_ptr<texture> a) : albedo(a) {}

  bool is_phase_function() const override { return true; }
  bool transmits() const override { return true; }

  color eval(const hit_record &rec, const vec3 &, const vec3 &) const override {
    return albedo->value(rec.u, rec.v, rec.p) * INV_4PI;
  }
  double pdf(const hit_record &, const vec3 &, const vec3 &) const override {
    return INV_4PI;
  }
  bool sample(const hit_record &rec, const vec3 &, double, double u1,
              double u2, bsdf_sample &out) const override {
    out.wi = sampling::square_to_uniform_sphere(u1, u2);
    out.pdf = INV_4PI;
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
