
#include "aabb.h"
#include "sphere.h"
#include <cmath>


// Get spherical UV coordinates from a point on unit sphere
// u: [0,1] around Y axis (longitude)
// v: [0,1] from bottom to top (latitude)
static void get_sphere_uv(const point3 &p, double &u, double &v) {
  // p: a point on the sphere of radius one, centered at the origin
  // Using spherical coordinates:
  // theta = angle down from +Y axis (0 to π)
  // phi = angle around Y axis (0 to 2π)

  double theta = acos(-p.y());
  double phi = atan2(-p.z(), p.x()) + M_PI;

  u = phi / (2 * M_PI);
  v = theta / M_PI;
}

bool sphere::hit(const ray &r, double t_min, double t_max,
                 hit_record &rec) const {
  vec3 oc = r.origin() - center;
  auto a = r.direction().length_squared();
  auto half_b = dot(oc, r.direction());
  auto c = oc.length_squared() - radius * radius;

  auto discriminant = half_b * half_b - a * c;
  if (discriminant < 0)
    return false;
  auto sqrtd = sqrt(discriminant);

  // Find the nearest root that lies in the acceptable range.
  auto root = (-half_b - sqrtd) / a;
  if (root < t_min || t_max < root) {
    root = (-half_b + sqrtd) / a;
    if (root < t_min || t_max < root)
      return false;
  }

  rec.t = root;
  rec.p = r.at(rec.t);
  vec3 outward_normal = (rec.p - center) / radius;
  rec.set_face_normal(r, outward_normal);
  rec.mat_ptr = mat_ptr;

  // Compute UV coordinates for texture mapping
  get_sphere_uv(outward_normal, rec.u, rec.v);

  return true;
}

bool sphere::bounding_box(aabb &output_box) const {
  output_box = aabb(center - vec3(radius, radius, radius),
                    center + vec3(radius, radius, radius));
  return true;
}
