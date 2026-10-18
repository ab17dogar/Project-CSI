#ifndef DIELECTRIC_H
#define DIELECTRIC_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include <cmath>

/**
 * @brief Smooth dielectric (glass/water) interface.
 *
 * Specular reflection and refraction chosen stochastically in proportion to
 * the exact unpolarized Fresnel reflectance (not Schlick's approximation).
 * Both lobes are Dirac deltas, so the material is invisible to light
 * sampling and has_smooth_lobes() is false.
 *
 * Refracted radiance is scaled by 1/eta^2 (radiance compression across the
 * interface, Veach 1997 sec. 5.2). For a closed object the factors cancel
 * between entry and exit; for an open surface such as the Cornell box water
 * they do not.
 *
 * `tint` scales transmission only. It is a per-crossing filter, not
 * Beer-Lambert absorption.
 *
 * Common refraction indices:
 * - Air: 1.0
 * - Water: 1.33
 * - Glass: 1.5
 * - Diamond: 2.4
 */
class dielectric : public material {
public:
  // Pure clear glass
  dielectric(double index_of_refraction)
      : ir(index_of_refraction), tint(1.0, 1.0, 1.0) {}

  // Tinted glass with color
  dielectric(double index_of_refraction, const color &glass_tint)
      : ir(index_of_refraction), tint(glass_tint) {}

  bool has_smooth_lobes() const override { return false; }
  bool transmits() const override { return true; }

  color eval(const hit_record &, const vec3 &, const vec3 &) const override {
    return color(0, 0, 0);
  }
  double pdf(const hit_record &, const vec3 &, const vec3 &) const override {
    return 0.0;
  }
  bool sample(const hit_record &rec, const vec3 &wo, double uc, double u1,
              double u2, bsdf_sample &out) const override;

public:
  double ir;  // Index of Refraction
  color tint; // Transmission filter (1,1,1 for clear glass)
};

#endif
