#ifndef MIS_H
#define MIS_H

#include "../util/vec3.h"
#include "hittable.h"
#include "pdf.h"
#include <cmath>
#include <memory>

/**
 * @brief Multiple Importance Sampling (MIS) utilities
 *
 * MIS combines multiple sampling strategies (e.g., BRDF sampling + light
 * sampling) to reduce variance. The key insight is that different sampling
 * strategies are better for different scenarios:
 * - BRDF sampling: good for rough surfaces
 * - Light sampling: good for small/bright lights
 *
 * MIS combines them optimally using the balance heuristic or power heuristic.
 */
namespace mis {

/**
 * @brief Balance heuristic for MIS
 *
 * w_s = p_s / (p_s + p_other)
 *
 * Simple but effective. Used when you have two sampling strategies.
 */
inline double balance_heuristic(double pdf_s, double pdf_other) {
  if (pdf_s + pdf_other < 1e-10)
    return 0.0;
  return pdf_s / (pdf_s + pdf_other);
}

/**
 * @brief Power heuristic for MIS (beta = 2)
 *
 * w_s = p_s^2 / (p_s^2 + p_other^2)
 *
 * Slightly better than balance heuristic in practice.
 * The power of 2 is the standard choice.
 */
inline double power_heuristic(double pdf_s, double pdf_other,
                              double beta = 2.0) {
  if (pdf_s < 1e-10)
    return 0.0;
  double ps = std::pow(pdf_s, beta);
  double po = std::pow(pdf_other, beta);
  if (ps + po < 1e-10)
    return 0.0;
  return ps / (ps + po);
}

/**
 * @brief Generalized power heuristic for n samples
 *
 * w_s = (n_s * p_s)^beta / sum((n_i * p_i)^beta)
 */
inline double power_heuristic_n(int n_s, double pdf_s, int n_other,
                                double pdf_other, double beta = 2.0) {
  double f = n_s * pdf_s;
  double g = n_other * pdf_other;

  double f_pow = std::pow(f, beta);
  double g_pow = std::pow(g, beta);

  if (f_pow + g_pow < 1e-10)
    return 0.0;

  return f_pow / (f_pow + g_pow);
}

/**
 * @brief Sample a direction toward a point light
 *
 * Returns the direction and the PDF value for that direction.
 */
inline vec3 sample_point_light(const point3 &hit_point, const point3 &light_pos,
                               double &pdf_out) {
  vec3 to_light = light_pos - hit_point;
  double dist_sq = to_light.length_squared();
  double dist = std::sqrt(dist_sq);

  // PDF for point light sampling is 1 (deterministic direction)
  // but we need to account for solid angle conversion
  // pdf_area_to_solid_angle = distance^2 / cos(theta_light)
  // For a point light with no area, we use a delta function approximation

  pdf_out = 1.0; // Will be weighted by 1/distance^2 in contribution

  return to_light / dist;
}

/**
 * @brief Compute the PDF for sampling a specific direction toward a light
 *
 * This is needed for MIS weighting when the direction was generated
 * by BRDF sampling but we need the light's pdf for that direction.
 */
inline double pdf_point_light(const point3 &hit_point, const point3 &light_pos,
                              const vec3 &sampled_direction,
                              double light_radius = 0.0) {
  // For a point light (radius = 0), the PDF is either 0 or infinite
  // depending on whether the direction hits the light exactly.
  // In practice, we treat small lights as having a small radius.

  if (light_radius < 0.001) {
    // Point light approximation - return a small value
    // to avoid division issues in MIS weight
    return 0.001;
  }

  // For an area light, compute solid angle
  vec3 to_light = light_pos - hit_point;
  double dist = to_light.length();
  double cos_theta_max =
      std::sqrt(1.0 - (light_radius * light_radius) / (dist * dist));

  // Uniform cone PDF
  return 1.0 / (2.0 * M_PI * (1.0 - cos_theta_max));
}

/**
 * @brief Compute cosine-weighted hemisphere PDF for a direction
 */
inline double pdf_cosine_hemisphere(const vec3 &direction, const vec3 &normal) {
  double cos_theta = dot(unit_vector(direction), normal);
  if (cos_theta <= 0)
    return 0.0;
  return cos_theta / M_PI;
}

/**
 * @brief Compute GGX BRDF PDF for a given direction
 */
inline double pdf_ggx(const vec3 &V, const vec3 &L, const vec3 &N,
                      double roughness) {
  vec3 H = unit_vector(V + L);
  double NdotH = std::max(0.0, dot(N, H));
  double VdotH = std::max(0.0, dot(V, H));

  if (VdotH < 0.001)
    return 0.0;

  // GGX NDF
  double a = roughness * roughness;
  double a2 = a * a;
  double NdotH2 = NdotH * NdotH;
  double denom = NdotH2 * (a2 - 1.0) + 1.0;
  double D = a2 / (M_PI * denom * denom);

  // Convert from half-vector PDF to light direction PDF
  // pdf_L = D * NdotH / (4 * VdotH)
  return D * NdotH / (4.0 * VdotH + 0.0001);
}

} // namespace mis

#endif
