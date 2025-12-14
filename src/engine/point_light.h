#pragma once

#include "../util/vec3.h"

class PointLight {
public:
  point3 position;
  color lightColor;
  double intensity;
  double radius; // For soft shadows

  PointLight(const point3 &pos, const color &col, double intens = 1.0,
             double rad = 0.0)
      : position(pos), lightColor(col), intensity(intens), radius(rad) {}

  // Calculate light contribution at a point
  color illuminate(const point3 &hitPoint, const vec3 &normal) const {
    vec3 toLight = position - hitPoint;
    double distance = toLight.length();
    vec3 lightDir = toLight / distance;

    // Lambertian falloff
    double cosTheta = std::max(0.0, dot(normal, lightDir));

    // Inverse square falloff
    double attenuation = intensity / (distance * distance);

    return lightColor * cosTheta * attenuation;
  }

  // Get direction from point to light
  vec3 directionFrom(const point3 &hitPoint) const {
    return unit_vector(position - hitPoint);
  }

  // Get distance from point to light
  double distanceFrom(const point3 &hitPoint) const {
    return (position - hitPoint).length();
  }
};
