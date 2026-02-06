#ifndef ONB_H
#define ONB_H

#include "../util/vec3.h"
#include <cmath>

/**
 * @brief Orthonormal Basis (ONB) for local coordinate systems
 *
 * Creates a local coordinate system from a single vector (typically a surface
 * normal). Used for importance sampling in materials - converts samples from
 * local space (where the normal is "up") to world space.
 */
class onb {
public:
  onb() {}

  /**
   * @brief Build ONB from a single "up" vector (typically surface normal)
   * @param n The vector that will become the local W axis
   */
  void build_from_w(const vec3 &n) {
    axis[2] = unit_vector(n);
    // Pick a vector not parallel to n
    vec3 a = (fabs(w().x()) > 0.9) ? vec3(0, 1, 0) : vec3(1, 0, 0);
    axis[1] = unit_vector(cross(w(), a));
    axis[0] = cross(w(), v());
  }

  // Access individual axes
  vec3 u() const { return axis[0]; }
  vec3 v() const { return axis[1]; }
  vec3 w() const { return axis[2]; }

  /**
   * @brief Transform a vector from local to world coordinates
   * @param a Vector in local coordinates (where w is "up")
   * @return Vector in world coordinates
   */
  vec3 local(double a, double b, double c) const {
    return a * u() + b * v() + c * w();
  }

  vec3 local(const vec3 &a) const {
    return a.x() * u() + a.y() * v() + a.z() * w();
  }

public:
  vec3 axis[3];
};

#endif
