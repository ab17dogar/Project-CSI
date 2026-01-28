#ifndef CONSTANT_MEDIUM_H
#define CONSTANT_MEDIUM_H

#include "hittable.h"
#include "isotropic.h"
#include "material.h"
#include "texture.h"
#include <cmath>

/**
 * @brief Volumetric medium (fog, smoke, mist) with constant density
 *
 * Uses exponential probability distribution to determine where
 * rays scatter within the volume. Works with isotropic material
 * for realistic volumetric scattering.
 */
class constant_medium : public hittable {
public:
  /**
   * @brief Create a constant density medium
   * @param b Boundary hittable (volume shape)
   * @param d Density of the medium (higher = more opaque)
   * @param a Albedo color
   */
  constant_medium(shared_ptr<hittable> b, double d, shared_ptr<texture> a)
      : boundary(b), neg_inv_density(-1 / d),
        phase_function(make_shared<isotropic>(a)) {}

  constant_medium(shared_ptr<hittable> b, double d, color c)
      : boundary(b), neg_inv_density(-1 / d),
        phase_function(make_shared<isotropic>(c)) {}

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override {
    // Enable debugging output occasionally
    const bool enableDebug = false;

    hit_record rec1, rec2;

    // Find where ray enters the boundary
    if (!boundary->hit(r, -std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::infinity(), rec1))
      return false;

    // Find where ray exits the boundary
    if (!boundary->hit(r, rec1.t + 0.0001,
                       std::numeric_limits<double>::infinity(), rec2))
      return false;

    if (enableDebug) {
      std::clog << "\nt_min=" << rec1.t << ", t_max=" << rec2.t << '\n';
    }

    // Clamp to ray interval
    if (rec1.t < t_min)
      rec1.t = t_min;
    if (rec2.t > t_max)
      rec2.t = t_max;

    if (rec1.t >= rec2.t)
      return false;

    if (rec1.t < 0)
      rec1.t = 0;

    // Calculate distance through the medium
    const auto ray_length = r.direction().length();
    const auto distance_inside_boundary = (rec2.t - rec1.t) * ray_length;

    // Random hit distance based on exponential distribution
    const auto hit_distance = neg_inv_density * log(random_double());

    // Check if scatter occurs within the volume
    if (hit_distance > distance_inside_boundary)
      return false;

    // Set up hit record for the scatter point
    rec.t = rec1.t + hit_distance / ray_length;
    rec.p = r.at(rec.t);

    if (enableDebug) {
      std::clog << "hit_distance = " << hit_distance << '\n'
                << "rec.t = " << rec.t << '\n'
                << "rec.p = " << rec.p << '\n';
    }

    // Arbitrary normal (not used for isotropic scattering)
    rec.normal = vec3(1, 0, 0);
    rec.front_face = true;
    rec.mat_ptr = phase_function;

    return true;
  }

  virtual bool bounding_box(aabb &output_box) const override {
    return boundary->bounding_box(output_box);
  }

public:
  shared_ptr<hittable> boundary;
  double neg_inv_density;
  shared_ptr<material> phase_function;
};

#endif
