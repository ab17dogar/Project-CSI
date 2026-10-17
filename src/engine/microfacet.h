#ifndef MICROFACET_H
#define MICROFACET_H

#include "../util/vec3.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Isotropic GGX / Trowbridge-Reitz microfacet distribution.
 *
 * All vectors are in the local shading frame (z = normal) and unit length.
 * alpha is the GGX width parameter; with the glTF / Disney convention
 * alpha = roughness^2.
 *
 * References:
 *  - Walter et al., "Microfacet Models for Refraction through Rough
 *    Surfaces", EGSR 2007 (D, G1).
 *  - Heitz, "Understanding the Masking-Shadowing Function in
 *    Microfacet-Based BRDFs", JCGT 2014 (height-correlated G2).
 *  - Heitz, "Sampling the GGX Distribution of Visible Normals", JCGT 2018.
 */
namespace microfacet {

inline double ggx_D(const vec3 &h, double alpha) {
  double cos2 = h.z() * h.z();
  if (cos2 <= 0.0)
    return 0.0;
  double a2 = alpha * alpha;
  double d = cos2 * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

// Smith Lambda for GGX.
inline double ggx_lambda(const vec3 &w, double alpha) {
  double cos2 = w.z() * w.z();
  if (cos2 <= 0.0)
    return 0.0;
  double tan2 = std::max(0.0, 1.0 - cos2) / cos2;
  return 0.5 * (-1.0 + std::sqrt(1.0 + alpha * alpha * tan2));
}

inline double ggx_G1(const vec3 &w, double alpha) {
  return 1.0 / (1.0 + ggx_lambda(w, alpha));
}

// Height-correlated masking-shadowing.
inline double ggx_G2(const vec3 &wo, const vec3 &wi, double alpha) {
  return 1.0 / (1.0 + ggx_lambda(wo, alpha) + ggx_lambda(wi, alpha));
}

/**
 * Sample a visible normal for view direction wo (wo.z > 0). The sampled
 * half-vector h has density D_wo(h) = G1(wo) * max(0, wo.h) * D(h) / wo.z.
 */
inline vec3 sample_vndf(const vec3 &wo, double alpha, double u1, double u2) {
  // Stretch the view vector to the hemisphere configuration.
  vec3 vh = unit_vector(vec3(alpha * wo.x(), alpha * wo.y(), wo.z()));
  double lensq = vh.x() * vh.x() + vh.y() * vh.y();
  vec3 t1 = lensq > 0.0 ? vec3(-vh.y(), vh.x(), 0.0) / std::sqrt(lensq)
                        : vec3(1.0, 0.0, 0.0);
  vec3 t2 = cross(vh, t1);
  // Uniform point on the projected disk, warped to the visible half.
  double r = std::sqrt(u1);
  double phi = 2.0 * PI * u2;
  double p1 = r * std::cos(phi);
  double p2 = r * std::sin(phi);
  double s = 0.5 * (1.0 + vh.z());
  p2 = (1.0 - s) * std::sqrt(std::max(0.0, 1.0 - p1 * p1)) + s * p2;
  vec3 nh = p1 * t1 + p2 * t2 +
            std::sqrt(std::max(0.0, 1.0 - p1 * p1 - p2 * p2)) * vh;
  // Unstretch.
  return unit_vector(
      vec3(alpha * nh.x(), alpha * nh.y(), std::max(1e-12, nh.z())));
}

// Density of reflecting wo into wi when h was drawn with sample_vndf.
inline double vndf_reflection_pdf(const vec3 &wo, const vec3 &wi,
                                  double alpha) {
  if (wo.z() <= 0.0 || wi.z() <= 0.0)
    return 0.0;
  vec3 h = wo + wi;
  if (h.length_squared() == 0.0)
    return 0.0;
  h = unit_vector(h);
  // D_wo(h) / (4 wo.h) = G1(wo) D(h) / (4 wo.z)
  return ggx_G1(wo, alpha) * ggx_D(h, alpha) / (4.0 * wo.z());
}

inline double schlick_weight(double cos_theta) {
  double m = std::clamp(1.0 - cos_theta, 0.0, 1.0);
  double m2 = m * m;
  return m2 * m2 * m;
}

inline color fresnel_schlick(const color &f0, double cos_theta) {
  return f0 + (color(1, 1, 1) - f0) * schlick_weight(cos_theta);
}

/**
 * Exact unpolarized Fresnel reflectance at a dielectric interface.
 * eta = n_transmitted / n_incident for light arriving with cos_i > 0;
 * a negative cos_i means the ray is on the other side.
 */
inline double fresnel_dielectric(double cos_i, double eta) {
  cos_i = std::clamp(cos_i, -1.0, 1.0);
  if (cos_i < 0.0) {
    eta = 1.0 / eta;
    cos_i = -cos_i;
  }
  double sin2_i = std::max(0.0, 1.0 - cos_i * cos_i);
  double sin2_t = sin2_i / (eta * eta);
  if (sin2_t >= 1.0)
    return 1.0; // total internal reflection
  double cos_t = std::sqrt(std::max(0.0, 1.0 - sin2_t));
  double r_parl = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
  double r_perp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
  return 0.5 * (r_parl * r_parl + r_perp * r_perp);
}

} // namespace microfacet

#endif
