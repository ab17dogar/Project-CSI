#include "hdri_environment.h"
#include "../3rdParty/stb_image.h"
#include "light.h"

#include <algorithm>

bool hdri_environment::load(const std::string &filename) {
  int components = 0;
  int w = 0, h = 0;

  if (stbi_is_hdr(filename.c_str())) {
    // Radiance RGBE / HDR: already linear, unbounded radiance values.
    float *img = stbi_loadf(filename.c_str(), &w, &h, &components, 3);
    if (!img) {
      std::cerr << "Failed to load environment: " << filename << std::endl;
      return false;
    }
    data.assign(img, img + static_cast<size_t>(w) * h * 3);
    stbi_image_free(img);
  } else {
    unsigned char *img = stbi_load(filename.c_str(), &w, &h, &components, 3);
    if (!img) {
      std::cerr << "Failed to load environment: " << filename << std::endl;
      return false;
    }
    data.resize(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0; i < data.size(); i++) {
      const double c = img[i] / 255.0; // exact sRGB EOTF
      data[i] = static_cast<float>(
          c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
    }
    stbi_image_free(img);
  }
  width = w;
  height = h;

  std::cerr << "Loaded environment: " << filename << " (" << width << "x"
            << height << ")" << std::endl;
  return true;
}

color hdri_environment::sample(const vec3 &direction) const {
  if (!is_valid()) {
    // Default sky gradient if no HDRI loaded
    double t = 0.5 * (direction.y() + 1.0);
    return (1.0 - t) * color(1.0, 1.0, 1.0) + t * color(0.5, 0.7, 1.0);
  }
  double u, v;
  direction_to_equirect(direction, rotation, u, v);
  const int i = std::clamp(static_cast<int>(u * width), 0, width - 1);
  const int j = std::clamp(static_cast<int>(v * height), 0, height - 1);
  return texel(i, j);
}
