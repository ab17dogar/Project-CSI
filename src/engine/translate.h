#ifndef TRANSLATE_H
#define TRANSLATE_H

#include "aabb.h"
#include "hittable.h"

/**
 * @brief Instance wrapper that translates a hittable by an offset
 *
 * Instead of moving all the geometry, we translate the ray in the
 * opposite direction and then translate the hit point back.
 */
class translate : public hittable {
public:
  translate(shared_ptr<hittable> p, const vec3 &displacement)
      : ptr(p), offset(displacement) {
    // Translate the bounding box
    aabb box;
    if (ptr->bounding_box(box)) {
      bbox = aabb(box.min() + offset, box.max() + offset);
    }
  }

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override {
    // Move the ray backwards by the offset
    ray moved_r(r.origin() - offset, r.direction(), r.time());

    // Determine whether an intersection exists along the offset ray
    if (!ptr->hit(moved_r, t_min, t_max, rec))
      return false;

    // Move the intersection point forwards by the offset. Translation keeps
    // directions, so the child's normal and front_face are already right.
    // (Re-running set_face_normal on the flipped normal, as before, always
    // reported a front-face hit and broke glass inside instances.)
    rec.p = rec.p + offset;
    rec.area_light = nullptr; // emitters inside instances are not sampled

    return true;
  }

  virtual bool hit_any(const ray &r, double t_min,
                       double t_max) const override {
    return ptr->hit_any(ray(r.origin() - offset, r.direction(), r.time()),
                        t_min, t_max);
  }

  virtual bool bounding_box(aabb &output_box) const override {
    output_box = bbox;
    return true;
  }

public:
  shared_ptr<hittable> ptr;
  vec3 offset;
  aabb bbox;
};

#endif
