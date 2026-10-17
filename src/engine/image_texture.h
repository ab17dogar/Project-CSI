#ifndef IMAGE_TEXTURE_H
#define IMAGE_TEXTURE_H

#include "texture.h"
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

// stb_image for loading - implementation is in stb_image_impl.cpp
#include "../3rdParty/stb_image.h"

/**
 * @brief Image-based texture loaded from file
 *
 * Supports PNG, JPG, and other common formats via stb_image.
 * Provides bilinear filtering for smooth sampling.
 */
class image_texture : public texture {
public:
  image_texture() : data(nullptr), width(0), height(0), bytes_per_scanline(0) {}

  image_texture(const std::string &filename) { load(filename); }

  ~image_texture() {
    if (data) {
      stbi_image_free(data);
    }
  }

  bool load(const std::string &filename) {
    int components_per_pixel = 3;
    data =
        stbi_load(filename.c_str(), &width, &height, &components_per_pixel, 3);

    if (!data) {
      std::cerr << "ERROR: Could not load texture image: " << filename
                << std::endl;
      width = height = 0;
      return false;
    }

    bytes_per_scanline = 3 * width;
    std::cerr << "Loaded texture: " << filename << " (" << width << "x"
              << height << ")" << std::endl;
    return true;
  }

  bool is_valid() const { return data != nullptr; }

  virtual color value(double u, double v, const point3 &p) const override {
    (void)p; // Unused for image textures

    // Return magenta for missing texture (easy to spot)
    if (!data) {
      return color(1.0, 0.0, 1.0);
    }

    // Clamp UV coordinates to [0,1] with repeat wrapping
    u = u - std::floor(u);
    v = v - std::floor(v);

    // Flip V coordinate (image is stored top-to-bottom)
    v = 1.0 - v;

    // Bilinear filtering: sample 4 neighboring pixels and interpolate
    double fx = u * width - 0.5;
    double fy = v * height - 0.5;

    int x0 = static_cast<int>(std::floor(fx));
    int y0 = static_cast<int>(std::floor(fy));
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // Fractional parts for interpolation
    double tx = fx - x0;
    double ty = fy - y0;

    // Sample 4 corners with wrapping
    color c00 = sample_pixel(x0, y0);
    color c10 = sample_pixel(x1, y0);
    color c01 = sample_pixel(x0, y1);
    color c11 = sample_pixel(x1, y1);

    // Bilinear interpolation
    color c0 = c00 * (1.0 - tx) + c10 * tx; // Top edge
    color c1 = c01 * (1.0 - tx) + c11 * tx; // Bottom edge
    return c0 * (1.0 - ty) + c1 * ty;       // Final blend
  }

  int get_width() const { return width; }
  int get_height() const { return height; }

private:
  // Helper to sample a single pixel with clamping
  color sample_pixel(int i, int j) const {
    // Clamp to valid range
    if (i < 0)
      i = 0;
    if (i >= width)
      i = width - 1;
    if (j < 0)
      j = 0;
    if (j >= height)
      j = height - 1;

    unsigned char *pixel = data + j * bytes_per_scanline + i * 3;
    if (!srgb) {
      const double color_scale = 1.0 / 255.0;
      return color(color_scale * pixel[0], color_scale * pixel[1],
                   color_scale * pixel[2]);
    }
    const double *lut = srgb_to_linear_table();
    return color(lut[pixel[0]], lut[pixel[1]], lut[pixel[2]]);
  }

  // 8-bit color textures are stored sRGB-encoded. Shading needs linear
  // reflectance, so decode with the exact sRGB EOTF (IEC 61966-2-1).
  static const double *srgb_to_linear_table() {
    static const auto table = [] {
      std::array<double, 256> t{};
      for (int i = 0; i < 256; ++i) {
        const double c = i / 255.0;
        t[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
      }
      return t;
    }();
    return table.data();
  }

public:
  // Treat texels as sRGB color (true for base-color maps; set false for
  // data textures such as roughness or normal maps).
  bool srgb = true;

private:

  unsigned char *data;
  int width, height;
  int bytes_per_scanline;
};

#endif
