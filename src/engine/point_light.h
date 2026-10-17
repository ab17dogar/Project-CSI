#pragma once

#include "../util/vec3.h"

/**
 * @brief Point light as described in a scene file.
 *
 * world::prepare() turns each entry into a point_light whose radiant
 * intensity is lightColor * intensity, so irradiance falls off as
 * cos(theta) * I / d^2. The shading itself lives in light.h.
 */
class PointLight {
public:
  point3 position;
  color lightColor;
  double intensity;
  double radius; // Reserved for spherical lights (not yet sampled)

  PointLight(const point3 &pos, const color &col, double intens = 1.0,
             double rad = 0.0)
      : position(pos), lightColor(col), intensity(intens), radius(rad) {}
};
