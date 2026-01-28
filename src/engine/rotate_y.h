#ifndef ROTATE_Y_H
#define ROTATE_Y_H

#include "aabb.h"
#include "hittable.h"
#include <cmath>
#include <limits>

/**
 * @brief Instance wrapper that rotates a hittable around the Y axis
 *
 * Useful for positioning objects in scenes without creating
 * new rotated geometry.
 */
class rotate_y : public hittable {
public:
  rotate_y(shared_ptr<hittable> p, double angle) : ptr(p) {
    auto radians = degrees_to_radians(angle);
    sin_theta = sin(radians);
    cos_theta = cos(radians);

    has_box = ptr->bounding_box(bbox);

    // Rotate the bounding box
    point3 min_p(std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity(),
                 std::numeric_limits<double>::infinity());
    point3 max_p(-std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity(),
                 -std::numeric_limits<double>::infinity());

    for (int i = 0; i < 2; i++) {
      for (int j = 0; j < 2; j++) {
        for (int k = 0; k < 2; k++) {
          auto x = i * bbox.max().x() + (1 - i) * bbox.min().x();
          auto y = j * bbox.max().y() + (1 - j) * bbox.min().y();
          auto z = k * bbox.max().z() + (1 - k) * bbox.min().z();

          auto newx = cos_theta * x + sin_theta * z;
          auto newz = -sin_theta * x + cos_theta * z;

          vec3 tester(newx, y, newz);

          for (int c = 0; c < 3; c++) {
            min_p[c] = fmin(min_p[c], tester[c]);
            max_p[c] = fmax(max_p[c], tester[c]);
          }
        }
      }
    }

    bbox = aabb(min_p, max_p);
  }

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override {
    // Rotate the ray origin and direction in the opposite direction
    auto origin = r.origin();
    auto direction = r.direction();

    origin[0] = cos_theta * r.origin()[0] - sin_theta * r.origin()[2];
    origin[2] = sin_theta * r.origin()[0] + cos_theta * r.origin()[2];

    direction[0] = cos_theta * r.direction()[0] - sin_theta * r.direction()[2];
    direction[2] = sin_theta * r.direction()[0] + cos_theta * r.direction()[2];

    ray rotated_r(origin, direction, r.time());

    // Determine whether an intersection exists in object space
    if (!ptr->hit(rotated_r, t_min, t_max, rec))
      return false;

    // Rotate the intersection point and normal back to world space
    auto p = rec.p;
    auto normal = rec.normal;

    p[0] = cos_theta * rec.p[0] + sin_theta * rec.p[2];
    p[2] = -sin_theta * rec.p[0] + cos_theta * rec.p[2];

    normal[0] = cos_theta * rec.normal[0] + sin_theta * rec.normal[2];
    normal[2] = -sin_theta * rec.normal[0] + cos_theta * rec.normal[2];

    rec.p = p;
    rec.set_face_normal(rotated_r, normal);

    return true;
  }

  virtual bool bounding_box(aabb &output_box) const override {
    output_box = bbox;
    return has_box;
  }

public:
  shared_ptr<hittable> ptr;
  double sin_theta;
  double cos_theta;
  bool has_box;
  aabb bbox;
};

#endif
