
#include "triangle.h"
#include "aabb.h"
#include "light.h"
#include "material.h"
#include <cmath>

// Constructor without UVs (backward compatible)
triangle::triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2,
                   shared_ptr<material> mat_ptr)
    : v0(v0), v1(v1), v2(v2), has_uvs(false), mat_ptr(mat_ptr) {
  precompute();
}

// Constructor with UVs for texture mapping
triangle::triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2,
                   const vec3 &uv0, const vec3 &uv1, const vec3 &uv2,
                   shared_ptr<material> mat_ptr)
    : v0(v0), v1(v1), v2(v2), uv0(uv0), uv1(uv1), uv2(uv2), has_uvs(true),
      mat_ptr(mat_ptr) {
  precompute();
}

void triangle::precompute() {
  e1 = v1 - v0;
  e2 = v2 - v0;
  const vec3 c = cross(e1, e2);
  const double len = c.length();
  // Relative test: a fixed threshold rejected every small triangle of a
  // finely tessellated mesh.
  const double scale = e1.length() * e2.length();
  degenerate = !(len > 1e-14 * scale) || !(len > 0.0);
  n_geom = degenerate ? vec3(0, 0, 1) : c / len;
}

double triangle::intersect(const ray &r, double t_min, double t_max,
                           double &b1, double &b2) const {
  if (degenerate)
    return -1.0;

  // Möller–Trumbore intersection algorithm with barycentric coordinates
  const vec3 h = cross(r.dir, e2);
  const double a = dot(e1, h);
  if (a == 0.0)
    return -1.0; // ray parallel to the triangle plane

  const double f = 1.0 / a;
  const vec3 s = r.orig - v0;
  b1 = f * dot(s, h);
  if (b1 < 0.0 || b1 > 1.0)
    return -1.0;

  const vec3 q = cross(s, e1);
  b2 = f * dot(r.dir, q);
  if (b2 < 0.0 || b1 + b2 > 1.0)
    return -1.0;

  const double t = f * dot(e2, q);
  if (t < t_min || t > t_max)
    return -1.0;
  return t;
}

bool triangle::hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const {
  double b1, b2;
  const double t = intersect(r, t_min, t_max, b1, b2);
  if (t < 0.0)
    return false;

  const double b0 = 1.0 - b1 - b2;
  rec.t = t;
  rec.p = b0 * v0 + b1 * v1 + b2 * v2; // on the plane, unlike o + t*d
  rec.set_face_normal(r, n_geom);
  rec.mat_ptr = mat_ptr.get();
  rec.area_light = emitter;

  // Interpolate UV coordinates using barycentric coordinates
  if (has_uvs) {
    rec.u = b0 * uv0.x() + b1 * uv1.x() + b2 * uv2.x();
    rec.v = b0 * uv0.y() + b1 * uv1.y() + b2 * uv2.y();
  } else {
    // Default UVs based on barycentric coordinates
    rec.u = b1;
    rec.v = b2;
  }
  return true;
}

bool triangle::hit_any(const ray &r, double t_min, double t_max) const {
  double b1, b2;
  return intersect(r, t_min, t_max, b1, b2) >= 0.0;
}

bool triangle::bounding_box(aabb &output_box) const {
  output_box = aabb(vmin(v0, vmin(v1, v2)), vmax(v0, vmax(v1, v2)));
  return true;
}

void triangle::collect_lights(std::vector<std::shared_ptr<light>> &out) {
  if (degenerate || !mat_ptr || !mat_ptr->is_emissive())
    return;
  auto l = area_light::make_triangle(v0, v1, v2, mat_ptr.get());
  emitter = l.get();
  out.push_back(std::move(l));
}
