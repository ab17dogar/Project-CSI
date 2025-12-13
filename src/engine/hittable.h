#ifndef HITTABLE_H
#define HITTABLE_H

#include "../util/ray.h"

class material;
class aabb;

struct hit_record {
  point3 p;
  vec3 normal;
  shared_ptr<material> mat_ptr;
  double t;
  double u; // Texture U coordinate
  double v; // Texture V coordinate
  bool front_face;

  inline void set_face_normal(const ray &r, const vec3 &outward_normal) {
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

class hittable {
public:
  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const = 0;

  // Compute bounding box for BVH acceleration
  // Returns true if the object has a finite bounding box, false otherwise
  virtual bool bounding_box(aabb &output_box) const = 0;
};

#endif
