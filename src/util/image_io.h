#ifndef IMAGE_IO_H
#define IMAGE_IO_H

#include <string>
#include <vector>

#include "vec3.h"

/**
 * @brief Linear-RGB float image, row-major with the top row first.
 *
 * This is the exchange format for everything measurement-related: renders,
 * references, per-pixel variance, and error maps.
 */
struct image_rgb {
  int width = 0;
  int height = 0;
  std::vector<float> data; // 3 floats per pixel

  image_rgb() = default;
  image_rgb(int w, int h, float fill = 0.0f)
      : width(w), height(h), data(static_cast<size_t>(w) * h * 3, fill) {}

  size_t pixel_count() const { return static_cast<size_t>(width) * height; }
  bool empty() const { return data.empty(); }

  color at(int x, int y) const {
    const size_t i = (static_cast<size_t>(y) * width + x) * 3;
    return color(data[i], data[i + 1], data[i + 2]);
  }
  void set(int x, int y, const color &c) {
    const size_t i = (static_cast<size_t>(y) * width + x) * 3;
    data[i] = static_cast<float>(c.x());
    data[i + 1] = static_cast<float>(c.y());
    data[i + 2] = static_cast<float>(c.z());
  }
};

// Portable float map (PFM). Read accepts color ("PF") and grayscale ("Pf")
// files of either endianness; write produces little-endian color.
bool write_pfm(const std::string &path, const image_rgb &img);
bool read_pfm(const std::string &path, image_rgb &out,
              std::string *error = nullptr);

// 8-bit PNG with the sRGB transfer function, after scaling by `exposure`.
bool write_png_srgb(const std::string &path, const image_rgb &img,
                    double exposure = 1.0);

// 8-bit PNG of already display-encoded values in [0,1] (e.g. a heat map).
bool write_png_display(const std::string &path, const image_rgb &img);

double linear_to_srgb(double c);
double srgb_to_linear(double c);

#endif
