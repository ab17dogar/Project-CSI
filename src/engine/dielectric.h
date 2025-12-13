#ifndef DIELECTRIC_H
#define DIELECTRIC_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include <cmath>

/**
 * @brief Dielectric (glass/transparent) material
 *
 * Implements refraction using Snell's law and reflection
 * using Schlick's approximation for realistic glass rendering.
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

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override;

public:
  double ir;  // Index of Refraction
  color tint; // Glass tint color (1,1,1 for clear glass)

private:
  // Schlick's approximation for reflectance
  static double reflectance(double cosine, double ref_idx) {
    double r0 = (1 - ref_idx) / (1 + ref_idx);
    r0 = r0 * r0;
    return r0 + (1 - r0) * pow((1 - cosine), 5);
  }
};

#endif
