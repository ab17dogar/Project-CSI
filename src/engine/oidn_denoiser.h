#ifndef OIDN_DENOISER_H
#define OIDN_DENOISER_H

#include "../util/vec3.h"
#include <iostream>
#include <string>
#include <vector>

// Check if OIDN is available
#ifdef USE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

/**
 * @brief Intel Open Image Denoise wrapper
 *
 * Provides high-quality AI-accelerated denoising for path-traced images.
 * Falls back to simple bilateral filter if OIDN is not available.
 */
class oidn_denoiser {
public:
  oidn_denoiser() = default;

  /**
   * @brief Denoise a rendered image
   *
   * @param input Input image (linear RGB)
   * @param width Image width
   * @param height Image height
   * @param hdr Whether input is HDR (> 1.0 values)
   * @return Denoised image
   */
  std::vector<color> denoise(const std::vector<color> &input, int width,
                             int height, bool hdr = true) const;

  /**
   * @brief Check if OIDN is available
   */
  static bool is_available();

  /**
   * @brief Get OIDN version string
   */
  static std::string version();
};

#endif
