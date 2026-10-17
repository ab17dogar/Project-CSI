
#include "aabb.h"
#include "light.h"
#include "material.h"
#include "sphere.h"
#include <algorithm>
#include <cmath>

// Get spherical UV coordinates from a point on unit sphere
// u: [0,1] around Y axis (longitude)
// v: [0,1] from bottom to top (latitude)
static void get_sphere_uv(const point3 &p, double &u, double &v) {
  // p: a point on the sphere of radius one, centered at the origin
  // Using spherical coordinates:
  // theta = angle down from +Y axis (0 to π)
  // phi = angle around Y axis (0 to 2π)

  double theta = acos(std::clamp(-p.y(), -1.0, 1.0));
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
  // Re-project onto the surface; o + t*d drifts off it for distant origins.
  rec.p = center + std::fabs(radius) * unit_vector(rec.p - center);
  rec.set_face_normal(r, unit_vector(outward_normal));
  rec.mat_ptr = mat_ptr.get();
  rec.area_light = emitter;

  // Compute UV coordinates for texture mapping
  get_sphere_uv(unit_vector(outward_normal), rec.u, rec.v);

  return true;
}

bool sphere::bounding_box(aabb &output_box) const {
  const double r = std::fabs(radius);
  output_box = aabb(center - vec3(r, r, r), center + vec3(r, r, r));
  return true;
}

void sphere::collect_lights(std::vector<std::shared_ptr<light>> &out) {
  // Negative radii (inside-out spheres) are not light-sampled; they still
  // emit when hit.
  if (!mat_ptr || !mat_ptr->is_emissive() || !(radius > 0.0))
    return;
  auto l = area_light::make_sphere(center, radius, mat_ptr.get());
  emitter = l.get();
  out.push_back(std::move(l));
}
