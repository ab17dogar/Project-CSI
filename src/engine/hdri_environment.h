#ifndef HDRI_ENVIRONMENT_H
#define HDRI_ENVIRONMENT_H

#include "../util/vec3.h"
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

/**
 * @brief HDRI Environment Map for Image-Based Lighting
 *
 * Loads equirectangular (lat/long) images and evaluates them as infinitely
 * distant lighting. Radiance .hdr files load as linear floats; 8-bit images
 * (PNG/JPG) are decoded from sRGB. Importance sampling lives in hdri_light.
 */
class hdri_environment {
public:
  hdri_environment() : intensity(1.0), rotation(0.0), width(0), height(0) {}

  ~hdri_environment() = default;

  /**
   * @brief Load environment map from file (.hdr, .png, .jpg, ...)
   */
  bool load(const std::string &filename);

  // Build from raw linear RGB texels (row-major, top row first). Used by tests.
  void set_pixels(int w, int h, std::vector<float> rgb) {
    width = w;
    height = h;
    data = std::move(rgb);
  }

  bool is_valid() const { return !data.empty(); }

  /**
   * @brief Radiance arriving from a direction (nearest texel, times intensity)
   */
  color sample(const vec3 &direction) const;

  // Linear radiance of one texel, including the intensity multiplier.
  color texel(int i, int j) const {
    const size_t idx = (static_cast<size_t>(j) * width + i) * 3;
    return color(data[idx], data[idx + 1], data[idx + 2]) * intensity;
  }

  int get_width() const { return width; }
  int get_height() const { return height; }

  // Intensity multiplier for the environment
  double intensity;

  // Rotation offset (radians around Y axis)
  double rotation;

private:
  std::vector<float> data; // Stored as linear RGB floats
  int width, height;
};

#endif
