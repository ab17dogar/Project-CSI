#ifndef MATERIAL_H
#define MATERIAL_H

// #include "rtweekend.h"
#include "../util/ray.h"

struct hit_record;

class material {
public:
  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const = 0;

  // Emission color for emissive materials (default: no emission)
  virtual color emitted(double u, double v, const point3 &p) const {
    return color(0, 0, 0);
  }

  virtual ~material() = default;
};

#endif