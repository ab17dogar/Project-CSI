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
 * Loads HDR/LDR images and samples them as infinite environment lighting.
 * Provides realistic lighting from real-world photographs.
 * Uses equirectangular mapping (lat/long format).
 */
class hdri_environment {
public:
  hdri_environment() : width(0), height(0), intensity(1.0), rotation(0.0) {}

  ~hdri_environment() = default;

  /**
   * @brief Load environment map from file
   * Supports PNG, JPG formats. HDR/EXR would need additional library.
   */
  bool load(const std::string &filename);

  bool is_valid() const { return !data.empty(); }

  /**
   * @brief Sample environment map from a direction
   * Uses equirectangular mapping (lat/long).
   */
  color sample(const vec3 &direction) const {
    if (!is_valid()) {
      // Default sky gradient if no HDRI loaded
      double t = 0.5 * (direction.y() + 1.0);
      return (1.0 - t) * color(1.0, 1.0, 1.0) + t * color(0.5, 0.7, 1.0);
    }

    // Apply rotation around Y axis
    vec3 dir = direction;
    if (rotation != 0.0) {
      double cos_r = cos(rotation);
      double sin_r = sin(rotation);
      dir = vec3(direction.x() * cos_r - direction.z() * sin_r, direction.y(),
                 direction.x() * sin_r + direction.z() * cos_r);
    }

    // Convert direction to spherical coordinates (equirectangular mapping)
    double theta = acos(fmax(-1.0, fmin(1.0, dir.y()))); // 0 to π
    double phi = atan2(dir.z(), dir.x());                // -π to π

    // Convert to UV coordinates
    double u = (phi + M_PI) / (2.0 * M_PI); // 0 to 1
    double v = theta / M_PI;                // 0 to 1

    // Convert to pixel coordinates
    int i = static_cast<int>(u * width) % width;
    int j = static_cast<int>(v * height) % height;
    if (i < 0)
      i += width;
    if (j < 0)
      j += height;

    int idx = (j * width + i) * 3;

    // Return color with intensity multiplier
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
