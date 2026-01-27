#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "material.h"

class emissive : public material {
public:
  emissive(const color &c) : pcolor(c) {}
  emissive(const color &c, double intensity) : pcolor(c * intensity) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override;

  // Emissive materials emit their color
  virtual color emitted(double u, double v, const point3 &p) const override {
    return pcolor;
  }

public:
  color pcolor;
};

#endif
