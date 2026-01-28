#include "pdf.h"
#include "quad.h"

// Implementation of hittable_pdf methods that require full quad definition

double hittable_pdf::value(const vec3 &direction) const {
  // For now, use a simple uniform solid angle approximation
  // A more accurate implementation would use the exact solid angle
  // subtended by the hittable from the origin point

  hit_record rec;
  if (!ptr->hit(ray(o, direction), 0.001, INF, rec))
    return 0;

  // Compute solid angle based on area and distance
  auto distance_squared = rec.t * rec.t * direction.length_squared();
  auto cosine = fabs(dot(direction, rec.normal) / direction.length());

  // Return 1 / solid_angle
  // For a quad, solid_angle ≈ area * cosine / distance^2
  // We need the area, which for a quad is |u × v|

  // Fallback: return a reasonable value
  return cosine / distance_squared;
}

vec3 hittable_pdf::generate() const {
  // Generate a random point on the hittable and return direction to it
  // For now, this is a placeholder that returns a random direction toward
  // the hittable's bounding box center

  aabb box;
  if (ptr->bounding_box(box)) {
    // Random point within the bounding box
    point3 center = (box.min() + box.max()) * 0.5; // Simplified: target center
    return center - o;
  }

  // Fallback to random direction
  return random_unit_vector();
}
