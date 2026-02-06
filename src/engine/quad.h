#ifndef QUAD_H
#define QUAD_H

#include "aabb.h"
#include "hittable.h"
#include "material.h"
#include <cmath>

/**
 * @brief Quad (parallelogram) primitive for planar surfaces
 *
 * Defined by a corner point Q and two edge vectors u and v.
 * The quad spans from Q to Q+u, Q+v, and Q+u+v.
 *
 * Used for:
 * - Cornell box walls
 * - Area lights
 * - Any planar rectangular surface
 */
class quad : public hittable {
public:
  quad() {}

  /**
   * @brief Construct a quad from corner point and edge vectors
   * @param _Q Corner point of the quad
   * @param _u First edge vector (Q to Q+u)
   * @param _v Second edge vector (Q to Q+v)
   * @param m Material for the quad
   */
  quad(const point3 &_Q, const vec3 &_u, const vec3 &_v, shared_ptr<material> m)
      : Q(_Q), u(_u), v(_v), mat(m) {
    // Precompute values for ray-plane intersection
    auto n = cross(u, v);
    normal = unit_vector(n);
    D = dot(normal, Q);
    w = n / dot(n, n);

    set_bounding_box();
  }

  virtual void set_bounding_box() {
    // Compute the bounding box of all four vertices
    auto bbox_diagonal1 = aabb(Q, Q + u + v);
    auto bbox_diagonal2 = aabb(Q + u, Q + v);
    bbox = surrounding_box(bbox_diagonal1, bbox_diagonal2);
  }

  virtual bool bounding_box(aabb &output_box) const override {
    output_box = bbox;
    return true;
  }

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override {
    auto denom = dot(normal, r.direction());

    // No hit if ray is parallel to the plane
    if (fabs(denom) < 1e-8)
      return false;

    // Return false if hit point is outside the ray interval
    auto t = (D - dot(normal, r.origin())) / denom;
    if (t < t_min || t > t_max)
      return false;

    // Determine if hit point lies within the quad
    auto intersection = r.at(t);
    vec3 planar_hitpt_vector = intersection - Q;
    auto alpha = dot(w, cross(planar_hitpt_vector, v));
    auto beta = dot(w, cross(u, planar_hitpt_vector));

    if (!is_interior(alpha, beta, rec))
      return false;

    // Ray hits the quad
    rec.t = t;
    rec.p = intersection;
    rec.mat_ptr = mat;
    rec.set_face_normal(r, normal);

    return true;
  }

  /**
   * @brief Check if hit point is inside the quad and set UV coords
   */
  virtual bool is_interior(double a, double b, hit_record &rec) const {
    // Given the hit point in plane coordinates (a,b), return false
    // if it's outside the primitive
    if ((a < 0) || (1 < a) || (b < 0) || (1 < b))
      return false;

    rec.u = a;
    rec.v = b;
    return true;
  }

public:
  point3 Q;  // Corner point
  vec3 u, v; // Edge vectors
  shared_ptr<material> mat;
  aabb bbox;
  vec3 normal; // Unit normal
  double D;    // Distance from origin to plane
  vec3 w;      // Precomputed value for intersection
};

#endif
