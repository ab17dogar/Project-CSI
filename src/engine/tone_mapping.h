#ifndef TONE_MAPPING_H
#define TONE_MAPPING_H

#include "../util/vec3.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Tone mapping utilities for converting HDR to LDR
 *
 * Provides various tone mapping operators for photorealistic rendering.
 */
namespace tone_mapping {

/**
 * @brief ACES Filmic tone mapping
 *
 * Industry-standard tone mapping curve used in movies.
 * Provides natural highlight rolloff and pleasing contrast.
 */
inline color aces_filmic(const color &hdr) {
  // ACES approximation by Krzysztof Narkowicz
  const double a = 2.51;
  const double b = 0.03;
  const double c = 2.43;
  const double d = 0.59;
  const double e = 0.14;

  auto tone_map_channel = [&](double x) {
    x = std::max(0.0, x);
    return std::clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
  };

  return color(tone_map_channel(hdr.x()), tone_map_channel(hdr.y()),
               tone_map_channel(hdr.z()));
}

/**
 * @brief Reinhard tone mapping
 *
 * Simple and effective for general use.
 */
inline color reinhard(const color &hdr) {
  return color(hdr.x() / (1.0 + hdr.x()), hdr.y() / (1.0 + hdr.y()),
               hdr.z() / (1.0 + hdr.z()));
}

/**
 * @brief Reinhard extended with white point
 *
 * Allows control over the brightest representable white.
 */
inline color reinhard_extended(const color &hdr, double white_point = 4.0) {
  double l_white = white_point * white_point;

  auto tone_map_channel = [&](double x) {
    return (x * (1.0 + x / l_white)) / (1.0 + x);
  };

  return color(std::clamp(tone_map_channel(hdr.x()), 0.0, 1.0),
               std::clamp(tone_map_channel(hdr.y()), 0.0, 1.0),
               std::clamp(tone_map_channel(hdr.z()), 0.0, 1.0));
}

/**
 * @brief Uncharted 2 filmic tone mapping
 *
 * Used in Uncharted 2, provides cinematic look.
 */
inline color uncharted2(const color &hdr) {
  auto uncharted_func = [](double x) {
    const double A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
  };

  const double exposure_bias = 2.0;
  const double W = 11.2; // Linear white point
  double white_scale = 1.0 / uncharted_func(W);

  return color(std::clamp(uncharted_func(hdr.x() * exposure_bias) * white_scale,
                          0.0, 1.0),
               std::clamp(uncharted_func(hdr.y() * exposure_bias) * white_scale,
                          0.0, 1.0),
               std::clamp(uncharted_func(hdr.z() * exposure_bias) * white_scale,
                          0.0, 1.0));
}

/**
 * @brief Apply gamma correction
 */
inline color gamma_correct(const color &linear, double gamma = 2.2) {
  double inv_gamma = 1.0 / gamma;
  return color(std::pow(std::max(0.0, linear.x()), inv_gamma),
               std::pow(std::max(0.0, linear.y()), inv_gamma),
               std::pow(std::max(0.0, linear.z()), inv_gamma));
}

/**
 * @brief Full post-processing pipeline
 *
 * Applies exposure, tone mapping, and gamma correction.
 */
inline color post_process(const color &hdr, double exposure = 1.0) {
  // Apply exposure
  color exposed = hdr * exposure;

  // Apply ACES tone mapping
  color tonemapped = aces_filmic(exposed);

  // Apply gamma correction (sRGB)
  return gamma_correct(tonemapped, 2.2);
}

} // namespace tone_mapping

#endif
