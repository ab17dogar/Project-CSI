
#include "camera.h"

camera::camera(point3 lookfrom, point3 lookat, vec3 vup,
               double vfov, // vertical field-of-view in degrees
               double aspect_ratio, double aperture_in, double focus_dist_in) {

  LOOK_FROM = lookfrom;
  LOOK_AT = lookat;
  UP = vup;
  FOV = vfov;
  ASPECT_RATIO = aspect_ratio;
  aperture = aperture_in;
  focus_dist = focus_dist_in;
  lens_radius = aperture / 2.0;

  auto theta = degrees_to_radians(vfov);
  auto h = tan(theta / 2);
  auto viewport_height = 2.0 * h;
  auto viewport_width = aspect_ratio * viewport_height;

  // Camera orthonormal basis
  w = unit_vector(lookfrom - lookat);
  u = unit_vector(cross(vup, w));
  v = cross(w, u);

  origin = lookfrom;
  // Scale by focus_dist for depth of field
  horizontal = focus_dist * viewport_width * u;
  vertical = focus_dist * viewport_height * v;
  lower_left_corner = origin - horizontal / 2 - vertical / 2 - focus_dist * w;
}

ray camera::get_ray(double s, double t) const {
  // Depth of field: jitter ray origin on lens disk
  vec3 rd = lens_radius * random_in_unit_disk();
  vec3 offset = u * rd.x() + v * rd.y();

  return ray(origin + offset, lower_left_corner + s * horizontal +
                                  t * vertical - origin - offset);
}
