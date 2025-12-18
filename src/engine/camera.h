#ifndef CAMERA_H
#define CAMERA_H

#include "../util/ray.h"
#include "../util/vec3.h"


class camera {
public:
  camera() {}

  camera(point3 lookfrom, point3 lookat, vec3 vup,
         double vfov, // vertical field-of-view in degrees
         double aspect_ratio,
         double aperture = 0.0,  // Lens aperture (0 = pinhole camera, no DoF)
         double focus_dist = 1.0 // Focus distance
  );
  ray get_ray(double s, double t) const;

public:
  double VIEWPORT_WIDTH;
  double VIEWPORT_HEIGHT;
  double FOCAL_LENGTH;

  // Not used in the actual Camera Algorithms.
  //  Mostly for debugging and reference :
  point3 LOOK_FROM;
  point3 LOOK_AT;
  vec3 UP;
  double FOV; // vertical field-of-view in degrees
  double ASPECT_RATIO;

  // Depth of field parameters
  double aperture = 0.0;
  double focus_dist = 1.0;

private:
  point3 origin;
  point3 lower_left_corner;
  vec3 horizontal;
  vec3 vertical;

  // Camera orthonormal basis for depth of field
  vec3 u, v, w;
  double lens_radius = 0.0;
};

#endif
