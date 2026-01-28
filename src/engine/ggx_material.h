#ifndef GGX_MATERIAL_H
#define GGX_MATERIAL_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "onb.h"
#include "pdf.h"
#include <cmath>

/**
 * @brief Disney/Principled BRDF using GGX (Trowbridge-Reitz) microfacet model
 *
 * This is a physically-based material that can represent a wide range of
 * real-world surfaces from rough plastic to polished metal.
 *
 * Key parameters:
 * - albedo: Base color of the material
 * - roughness: 0.0 = perfectly smooth (mirror), 1.0 = fully rough (diffuse)
 * - metallic: 0.0 = dielectric (plastic), 1.0 = metal (no diffuse)
 *
 * The model combines:
 * - GGX Normal Distribution Function (D)
 * - Schlick-GGX Geometry Function (G)
 * - Fresnel-Schlick approximation (F)
 */
class ggx_material : public material {
public:
  ggx_material(const color &a, double rough, double metal = 0.0)
      : albedo(a), roughness(std::max(0.04, rough)), // Clamp to avoid division
                                                     // by zero
        metallic(metal) {}

  virtual bool scatter(const ray &r_in, const hit_record &rec,
                       color &attenuation, ray &scattered) const override {
    // View direction (pointing away from surface)
    vec3 V = unit_vector(-r_in.direction());
    vec3 N = rec.normal;

    // Ensure we're on the correct side
    if (dot(V, N) < 0) {
      N = -N;
    }

    // Sample microfacet normal using GGX distribution
    vec3 H = sample_ggx_vndf(V, N, roughness);

    // Reflect view direction around microfacet normal
    vec3 L = reflect(-V, H);

    // Check if reflection is above surface
    if (dot(L, N) <= 0) {
      // For rough surfaces, fall back to diffuse
      if (roughness > 0.5) {
        L = N + random_unit_vector();
        if (L.near_zero())
          L = N;
        L = unit_vector(L);
      } else {
        return false;
      }
    }

    // Calculate BRDF components
    double NdotV = std::max(0.001, dot(N, V));
    double NdotL = std::max(0.001, dot(N, L));
    double NdotH = std::max(0.001, dot(N, H));
    double VdotH = std::max(0.001, dot(V, H));

    // Fresnel (Schlick approximation)
    // F0 = 0.04 for dielectrics, albedo for metals
    color F0 = color(0.04, 0.04, 0.04) * (1.0 - metallic) + albedo * metallic;
    color F = fresnel_schlick(VdotH, F0);

    // Distribution (GGX/Trowbridge-Reitz)
    double D = distribution_ggx(NdotH, roughness);

    // Geometry (Smith's method with GGX)
    double G = geometry_smith(NdotV, NdotL, roughness);

    // Cook-Torrance specular BRDF
    color specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.0001);

    // Diffuse component (only for non-metals)
    // Energy conservation: diffuse is reduced by Fresnel reflection
    color kD = (color(1, 1, 1) - F) * (1.0 - metallic);
    color diffuse = kD * albedo / M_PI;

    // Combined BRDF * NdotL
    attenuation = (diffuse + specular) * NdotL;

    // Clamp to prevent fireflies
    attenuation =
        color(std::min(attenuation.x(), 10.0), std::min(attenuation.y(), 10.0),
              std::min(attenuation.z(), 10.0));

    scattered = ray(rec.p, L, r_in.time());
    return true;
  }

private:
  color albedo;
  double roughness;
  double metallic;

  /**
   * @brief GGX (Trowbridge-Reitz) Normal Distribution Function
   *
   * Describes the statistical distribution of microfacet normals.
   */
  static double distribution_ggx(double NdotH, double roughness) {
    double a = roughness * roughness;
    double a2 = a * a;
    double NdotH2 = NdotH * NdotH;

    double num = a2;
    double denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = M_PI * denom * denom;

    return num / denom;
  }

  /**
   * @brief Schlick-GGX Geometry function (single direction)
   */
  static double geometry_schlick_ggx(double NdotX, double roughness) {
    double r = roughness + 1.0;
    double k = (r * r) / 8.0; // For direct lighting

    double num = NdotX;
    double denom = NdotX * (1.0 - k) + k;

    return num / denom;
  }

  /**
   * @brief Smith's method combining geometry for both directions
   */
  static double geometry_smith(double NdotV, double NdotL, double roughness) {
    double ggx1 = geometry_schlick_ggx(NdotV, roughness);
    double ggx2 = geometry_schlick_ggx(NdotL, roughness);
    return ggx1 * ggx2;
  }

  /**
   * @brief Fresnel-Schlick approximation
   */
  static color fresnel_schlick(double cosTheta, const color &F0) {
    double t = std::pow(1.0 - cosTheta, 5.0);
    return F0 + (color(1, 1, 1) - F0) * t;
  }

  /**
   * @brief Sample GGX VNDF (Visible Normal Distribution Function)
   *
   * More efficient importance sampling for GGX.
   */
  static vec3 sample_ggx_vndf(const vec3 &V, const vec3 &N, double roughness) {
    // Build orthonormal basis around N
    onb uvw;
    uvw.build_from_w(N);

    // Transform V to local space
    vec3 Vh = unit_vector(vec3(roughness * dot(V, uvw.u()),
                               roughness * dot(V, uvw.v()), dot(V, uvw.w())));

    // Sample GGX NDF
    double r1 = random_double();
    double r2 = random_double();

    double a = roughness * roughness;
    double phi = 2.0 * M_PI * r1;
    double cosTheta = sqrt((1.0 - r2) / (1.0 + (a * a - 1.0) * r2));
    double sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Spherical to Cartesian (local space)
    vec3 H_local(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Transform back to world space
    return unit_vector(uvw.local(H_local));
  }

  /**
   * @brief Reflect vector v around normal n
   */
  static vec3 reflect(const vec3 &v, const vec3 &n) {
    return v - 2 * dot(v, n) * n;
  }
};

#endif
