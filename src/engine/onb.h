#ifndef ONB_H
#define ONB_H

#include "../util/vec3.h"
#include <cmath>

/**
 * @brief Orthonormal Basis (ONB) for local coordinate systems
 *
 * Creates a local coordinate system from a single vector (typically a surface
 * normal). Used for importance sampling in materials - converts samples from
 * local space (where the normal is "up") to world space and back.
 *
 * The tangent frame uses the branchless construction of Duff et al.,
 * "Building an Orthonormal Basis, Revisited" (JCGT 2017), which stays
 * accurate for normals near -z where Frisvad's original method breaks down.
 */
class onb {
public:
  onb() {}
  explicit onb(const vec3 &n) { build_from_w(n); }

  /**
   * @brief Build ONB from a single "up" vector (typically surface normal)
   * @param n The vector that will become the local W axis
   */
  void build_from_w(const vec3 &n) {
    axis[2] = unit_vector(n);
    const vec3 &w_ = axis[2];
    double sign = std::copysign(1.0, w_.z());
    double a = -1.0 / (sign + w_.z());
    double b = w_.x() * w_.y() * a;
    axis[0] = vec3(1.0 + sign * w_.x() * w_.x() * a, sign * b, -sign * w_.x());
    axis[1] = vec3(b, sign + w_.y() * w_.y() * a, -w_.y());
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
    return a * axis[0] + b * axis[1] + c * axis[2];
  }

  vec3 local(const vec3 &a) const { return local(a.x(), a.y(), a.z()); }

  // World-space vector expressed in this basis (inverse of local()).
  vec3 to_local(const vec3 &a) const {
    return vec3(dot(a, axis[0]), dot(a, axis[1]), dot(a, axis[2]));
  }

public:
  vec3 axis[3];
};

#endif
