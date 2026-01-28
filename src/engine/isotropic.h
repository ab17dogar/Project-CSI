#ifndef ISOTROPIC_H
#define ISOTROPIC_H

#include "material.h"
#include "texture.h"

/**
 * @brief Isotropic scattering material for volumetric effects
 *
 * Scatters light uniformly in all directions regardless of
 * incoming ray direction. Used inside constant_medium volumes
 * for fog, smoke, and mist effects.
 */
class isotropic : public material {
public:
  isotropic(color c) : albedo(make_shared<solid_color>(c)) {}
  isotropic(shared_ptr<texture> a) : albedo(a) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override {
    // Scatter in a random direction (isotropic)
    scattered = ray(rec.p, random_unit_vector(), r_in.time());
    attenuation = albedo->value(rec.u, rec.v, rec.p);
    return true;
  }

public:
  shared_ptr<texture> albedo;
};

#endif
