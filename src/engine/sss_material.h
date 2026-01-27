#ifndef SSS_MATERIAL_H
#define SSS_MATERIAL_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "texture.h"
#include <cmath>
#include <memory>

/**
 * @brief Subsurface Scattering (SSS) material
 *
 * Simulates light penetrating into translucent materials like
 * skin, marble, wax, and other organic materials.
 *
 * Uses a simplified diffusion approximation for SSS effect.
 */
class sss_material : public material {
public:
  sss_material(const color &surface_color, const color &scatter_color,
               float scatter_distance = 0.5f, float roughness = 0.3f)
      : surface_albedo(make_shared<solid_color>(surface_color)),
        scatter_color(scatter_color), scatter_distance(scatter_distance),
        roughness(std::max(0.04f, roughness)) {}

  sss_material(shared_ptr<texture> albedo_tex, const color &scatter_color,
               float scatter_distance = 0.5f, float roughness = 0.3f)
      : surface_albedo(albedo_tex), scatter_color(scatter_color),
        scatter_distance(scatter_distance),
        roughness(std::max(0.04f, roughness)) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override {
    // Sample surface texture
    color base_color = surface_albedo->value(rec.u, rec.v, rec.p);

    vec3 unit_direction = unit_vector(r_in.direction());
    double cos_theta = fmin(dot(-unit_direction, rec.normal), 1.0);

    // Fresnel term for surface reflection
    double fresnel = schlick_fresnel(cos_theta, 0.04);

    vec3 scatter_direction;

    if (random_double() < fresnel * (1.0 - roughness * 0.5)) {
      // Surface specular reflection
      vec3 reflected = reflect(unit_direction, rec.normal);
      scatter_direction = reflected + roughness * random_in_unit_sphere();
      if (scatter_direction.near_zero())
        scatter_direction = rec.normal;
      attenuation = color(1, 1, 1); // White specular
    } else {
      // Subsurface scattering simulation
      // Mix of diffuse scatter with forward scattering for SSS approximation

      // Blend between surface and subsurface color
      double sss_factor = 0.4 * (1.0 - cos_theta); // More SSS at grazing angles
      color mixed_color =
          (1.0 - sss_factor) * base_color + sss_factor * scatter_color;

      // Slightly biased random direction for forward scattering
      vec3 random_dir = random_unit_vector();
      vec3 forward_bias = -unit_direction * 0.2; // Slight forward push
      scatter_direction = rec.normal + random_dir + forward_bias;

      if (scatter_direction.near_zero())
        scatter_direction = rec.normal;

      attenuation = mixed_color;
    }

    scattered = ray(rec.p, unit_vector(scatter_direction));
    return (dot(scattered.direction(), rec.normal) > 0) ||
           (random_double() < 0.1);
  }

public:
  shared_ptr<texture> surface_albedo;
  color scatter_color;    // Color of light scattering inside material
  float scatter_distance; // How far light travels inside
  float roughness;

private:
  static double schlick_fresnel(double cosine, double f0) {
    return f0 + (1.0 - f0) * pow((1.0 - cosine), 5);
  }
};

#endif
