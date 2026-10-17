
#include "vec3.h"
#include <cmath>
#include <iostream>

using namespace std;

std::ostream &operator<<(std::ostream &out, const vec3 &v) {
  return out << v.e[0] << ' ' << v.e[1] << ' ' << v.e[2];
}

vec3 random_in_unit_sphere() {
  while (true) {
    auto p = vec3::random(-1, 1);
    if (p.length_squared() >= 1)
      continue;
    return p;
  }
}

vec3 random_unit_vector() { return unit_vector(random_in_unit_sphere()); }

vec3 random_in_hemisphere(const vec3 &normal) {
  vec3 in_unit_sphere = random_in_unit_sphere();
  if (dot(in_unit_sphere, normal) > 0.0) // In the same hemisphere as the normal
    return in_unit_sphere;
  else
    return -in_unit_sphere;
}

vec3 refract(const vec3 &uv, const vec3 &n, double etai_over_etat) {
  auto cos_theta = fmin(dot(-uv, n), 1.0);
  vec3 r_out_perp = etai_over_etat * (uv + cos_theta * n);
  vec3 r_out_parallel = -sqrt(fabs(1.0 - r_out_perp.length_squared())) * n;
  return r_out_perp + r_out_parallel;
}

// Cosine-weighted hemisphere sampling for importance sampling
// Generates samples with probability proportional to cos(theta)
// This leads to lower variance when combined with Lambertian BRDFs
vec3 random_cosine_direction() {
  double r1 = random_double();
  double r2 = random_double();

  double z = sqrt(1.0 - r2);
  double phi = 2.0 * PI * r1;
  double x = cos(phi) * sqrt(r2);
  double y = sin(phi) * sqrt(r2);

  return vec3(x, y, z);
}

// Random point in unit disk for depth of field
vec3 random_in_unit_disk() {
  while (true) {
    auto p = vec3(random_double(-1, 1), random_double(-1, 1), 0);
    if (p.length_squared() >= 1)
      continue;
    return p;
  }
}
