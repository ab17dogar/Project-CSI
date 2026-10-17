#ifndef LAMBERTIAN_H
#define LAMBERTIAN_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"

class lambertian : public material {
public:
  lambertian(const color &a) : albedo(a) {}

  color eval(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override;
  double pdf(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override;
  bool sample(const hit_record &rec, const vec3 &wo, double uc, double u1,
              double u2, bsdf_sample &out) const override;

public:
  color albedo;
};

#endif
