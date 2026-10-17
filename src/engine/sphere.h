#ifndef SPHERE_H
#define SPHERE_H

#include "aabb.h"
#include "hittable.h"

class sphere : public hittable {
public:
  sphere() {}
  sphere(point3 cen, double r, shared_ptr<material> m)
      : center(cen), radius(r), mat_ptr(m) {};

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool bounding_box(aabb &output_box) const override;
  virtual void
  collect_lights(std::vector<std::shared_ptr<light>> &out) override;

public:
  point3 center;
  double radius;
  shared_ptr<material> mat_ptr;
  const light *emitter = nullptr;
};

#endif
