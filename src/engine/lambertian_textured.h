#ifndef LAMBERTIAN_TEXTURED_H
#define LAMBERTIAN_TEXTURED_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "texture.h"

/**
 * @brief Lambertian (diffuse) material with texture support
 *
 * This material uses a texture to define the albedo color at each point,
 * enabling detailed surface coloring from image textures.
 */
class lambertian_textured : public material {
public:
  lambertian_textured(shared_ptr<texture> a) : albedo(a) {}

  // Convenience constructor for solid color (wraps in solid_color texture)
  lambertian_textured(const color &a) : albedo(make_shared<solid_color>(a)) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override {
    (void)r_in; // Unused

    // Random scatter direction in hemisphere
    vec3 scatter_direction = rec.normal + random_unit_vector();

    // Catch degenerate scatter direction
    if (scatter_direction.near_zero())
      scatter_direction = rec.normal;

    scattered = ray(rec.p, scatter_direction);

    // Sample texture at UV coordinates
    attenuation = albedo->value(rec.u, rec.v, rec.p);

    return true;
  }

public:
  shared_ptr<texture> albedo;
};

#endif
