#include "dielectric.h"
#include "../util/vec3.h"
#include "hittable.h"
#include <cstdlib>

bool dielectric::scatter(const ray &r_in, const hit_record &rec,
                         color &attenuation, ray &scattered) const {
  // Glass tint (white = clear, can be tinted for colored glass)
  attenuation = tint;

  // Determine if we're entering or exiting the material
  double refraction_ratio = rec.front_face ? (1.0 / ir) : ir;

  vec3 unit_direction = unit_vector(r_in.direction());

  // Calculate cosine of incident angle
  double cos_theta = fmin(dot(-unit_direction, rec.normal), 1.0);
  double sin_theta = sqrt(1.0 - cos_theta * cos_theta);

  // Check for total internal reflection
  bool cannot_refract = refraction_ratio * sin_theta > 1.0;

  vec3 direction;

  if (cannot_refract ||
      reflectance(cos_theta, refraction_ratio) > random_double()) {
    // Must reflect (total internal reflection or Schlick)
    direction = reflect(unit_direction, rec.normal);
  } else {
    // Can refract
    direction = refract(unit_direction, rec.normal, refraction_ratio);
  }

  scattered = ray(rec.p, direction);
  return true;
}
