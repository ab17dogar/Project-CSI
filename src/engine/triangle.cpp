
#include "triangle.h"
#include "aabb.h"
#include <cmath>
#include <iostream>

// Constructor without UVs (backward compatible)
triangle::triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2,
                   shared_ptr<material> mat_ptr)
    : v0(v0), v1(v1), v2(v2), mat_ptr(mat_ptr), has_uvs(false) {
  // Check for degenerate triangles (zero area)
  vec3 edge1 = v1 - v0;
  vec3 edge2 = v2 - v0;
  vec3 normal = cross(edge1, edge2);
  float areaSquared = normal.length();
  const float threshold = 1e-8f;
  if (areaSquared < threshold) {
    degenerate = true;
    std::cerr << "Warning: created degenerate triangle (area ~ 0)."
              << std::endl;
  }
}

// Constructor with UVs for texture mapping
triangle::triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2,
                   const vec3 &uv0, const vec3 &uv1, const vec3 &uv2,
                   shared_ptr<material> mat_ptr)
    : v0(v0), v1(v1), v2(v2), uv0(uv0), uv1(uv1), uv2(uv2), has_uvs(true),
      mat_ptr(mat_ptr), degenerate(false) {
  // Check for degenerate triangles
  vec3 edge1 = v1 - v0;
  vec3 edge2 = v2 - v0;
  vec3 normal = cross(edge1, edge2);
  float areaSquared = normal.length();
  const float threshold = 1e-8f;
  if (areaSquared < threshold) {
    degenerate = true;
    std::cerr << "Warning: created degenerate triangle (area ~ 0)."
              << std::endl;
  }
}

bool triangle::hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const {
  if (degenerate)
    return false;

  // Möller–Trumbore intersection algorithm with barycentric coordinates
  vec3 edge1 = v1 - v0;
  vec3 edge2 = v2 - v0;
  vec3 h = cross(r.dir, edge2);
  double a = dot(edge1, h);

  if (fabs(a) < EPSILON)
    return false; // Ray parallel to triangle

  double f = 1.0 / a;
  vec3 s = r.orig - v0;
  double u_bary = f * dot(s, h);

  if (u_bary < 0.0 || u_bary > 1.0)
    return false;

  vec3 q = cross(s, edge1);
  double v_bary = f * dot(r.dir, q);

  if (v_bary < 0.0 || u_bary + v_bary > 1.0)
    return false;

  double t = f * dot(edge2, q);

  if (t < t_min || t > t_max)
    return false;

  // Valid hit - compute barycentric coordinate w
  double w_bary = 1.0 - u_bary - v_bary;

  rec.t = t;
  rec.p = r.orig + t * r.dir;

  // Compute normal
  vec3 outward_normal = unit_vector(cross(edge1, edge2));
  rec.set_face_normal(r, outward_normal);
  rec.mat_ptr = mat_ptr;

  // Interpolate UV coordinates using barycentric coordinates
  if (has_uvs) {
    // UV = w*uv0 + u*uv1 + v*uv2
    rec.u = w_bary * uv0.x() + u_bary * uv1.x() + v_bary * uv2.x();
    rec.v = w_bary * uv0.y() + u_bary * uv1.y() + v_bary * uv2.y();
  } else {
    // Default UVs based on barycentric coordinates
    rec.u = u_bary;
    rec.v = v_bary;
  }

  return true;
}

bool triangle::bounding_box(aabb &output_box) const {
  const double padding = 0.0001;

  point3 min_point(fmin(fmin(v0.x(), v1.x()), v2.x()) - padding,
                   fmin(fmin(v0.y(), v1.y()), v2.y()) - padding,
                   fmin(fmin(v0.z(), v1.z()), v2.z()) - padding);

  point3 max_point(fmax(fmax(v0.x(), v1.x()), v2.x()) + padding,
                   fmax(fmax(v0.y(), v1.y()), v2.y()) + padding,
                   fmax(fmax(v0.z(), v1.z()), v2.z()) + padding);

  output_box = aabb(min_point, max_point);
  return true;
}