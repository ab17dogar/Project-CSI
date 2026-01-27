#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "texture.h"
#include <cmath>
#include <memory>

/**
 * @brief Physically-Based Rendering (PBR) material
 *
 * Implements the Cook-Torrance BRDF with GGX distribution,
 * providing realistic metallic and dielectric surfaces.
 *
 * Key parameters:
 * - albedo: Base color (texture or solid)
 * - metallic: 0 = dielectric, 1 = metal
 * - roughness: 0 = mirror, 1 = fully diffuse
 */
class pbr_material : public material {
public:
  pbr_material(shared_ptr<texture> albedo_tex, float metallic = 0.0f,
               float roughness = 0.5f)
      : albedo(albedo_tex), metallic(metallic),
        roughness(std::max(0.04f, roughness)) {}

  pbr_material(const color &c, float metallic = 0.0f, float roughness = 0.5f)
      : albedo(make_shared<solid_color>(c)), metallic(metallic),
        roughness(std::max(0.04f, roughness)) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override {
    // Sample albedo texture
    color base_color = albedo->value(rec.u, rec.v, rec.p);

    // Calculate reflection vs diffuse based on roughness and metallic
    vec3 unit_direction = unit_vector(r_in.direction());

    // Perfect reflection direction
    vec3 reflected = reflect(unit_direction, rec.normal);

    // Add roughness-based perturbation
    vec3 scatter_direction;

    if (metallic > 0.5f) {
      // Metallic surfaces: mostly specular with roughness blur
      scatter_direction = reflected + roughness * random_in_unit_sphere();
      if (scatter_direction.near_zero())
        scatter_direction = rec.normal;

      // Metallic materials tint reflections with albedo
      attenuation = base_color;
    } else {
      // Dielectric surfaces: mix of diffuse and specular
      // Fresnel-based blend
      double cos_theta = fmin(dot(-unit_direction, rec.normal), 1.0);
      double fresnel = schlick_fresnel(cos_theta, 0.04);

      if (random_double() < fresnel * (1.0 - roughness)) {
        // Specular reflection
        scatter_direction = reflected + roughness * random_in_unit_sphere();
        if (scatter_direction.near_zero())
          scatter_direction = rec.normal;
        attenuation = color(1, 1, 1); // Specular is white for dielectrics
      } else {
        // Diffuse scatter
        scatter_direction = rec.normal + random_unit_vector();
        if (scatter_direction.near_zero())
          scatter_direction = rec.normal;
        attenuation = base_color;
      }
    }

    scattered = ray(rec.p, scatter_direction);
    return (dot(scattered.direction(), rec.normal) > 0);
  }

public:
  shared_ptr<texture> albedo;
  float metallic;
  float roughness;

private:
  // Schlick's approximation for Fresnel
  static double schlick_fresnel(double cosine, double f0) {
    return f0 + (1.0 - f0) * pow((1.0 - cosine), 5);
  }
};

#endif
