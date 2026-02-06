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

    // Move the intersection point forwards by the offset
    rec.p = rec.p + offset;
    rec.set_face_normal(moved_r, rec.normal);

    return true;
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
