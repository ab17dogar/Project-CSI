#ifndef IMAGE_TEXTURE_H
#define IMAGE_TEXTURE_H

#include "texture.h"
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

    // Convert to pixel coordinates
    int i = static_cast<int>(u * width);
    int j = static_cast<int>(v * height);

    // Clamp to valid range
    if (i >= width)
      i = width - 1;
    if (j >= height)
      j = height - 1;
    if (i < 0)
      i = 0;
    if (j < 0)
      j = 0;

    const double color_scale = 1.0 / 255.0;
    unsigned char *pixel = data + j * bytes_per_scanline + i * 3;

    return color(color_scale * pixel[0], color_scale * pixel[1],
                 color_scale * pixel[2]);
  }

  int get_width() const { return width; }
  int get_height() const { return height; }

private:
  unsigned char *data;
  int width, height;
  int bytes_per_scanline;
};

#endif
