#ifndef TEXTURE_H
#define TEXTURE_H

#include "../util/vec3.h"

/**
 * @brief Base class for all textures
 *
 * Textures return a color value given UV coordinates and a point in space.
 * This enables procedural textures, image textures, and more.
 */
class texture {
public:
  virtual ~texture() = default;

  /**
   * @brief Get the color value at the given UV coordinates
   * @param u U coordinate (0-1 range, wraps)
   * @param v V coordinate (0-1 range, wraps)
   * @param p Point in 3D space (for procedural textures)
   * @return Color at this location
   */
  virtual color value(double u, double v, const point3 &p) const = 0;
};

/**
 * @brief Simple solid color texture
 */
class solid_color : public texture {
public:
  solid_color() : color_value(0, 0, 0) {}
  solid_color(color c) : color_value(c) {}
  solid_color(double r, double g, double b) : color_value(r, g, b) {}

  virtual color value(double u, double v, const point3 &p) const override {
    (void)u;
    (void)v;
    (void)p; // Unused
    return color_value;
  }

private:
  color color_value;
};

/**
 * @brief Checker pattern texture for testing
 */
class checker_texture : public texture {
public:
  checker_texture() {}
  checker_texture(shared_ptr<texture> even, shared_ptr<texture> odd,
                  double scale = 10.0)
      : even_tex(even), odd_tex(odd), inv_scale(1.0 / scale) {}

  checker_texture(color c1, color c2, double scale = 10.0)
      : even_tex(make_shared<solid_color>(c1)),
        odd_tex(make_shared<solid_color>(c2)), inv_scale(1.0 / scale) {}

  virtual color value(double u, double v, const point3 &p) const override {
    int x_int = static_cast<int>(std::floor(inv_scale * p.x()));
    int y_int = static_cast<int>(std::floor(inv_scale * p.y()));
    int z_int = static_cast<int>(std::floor(inv_scale * p.z()));

    bool is_even = (x_int + y_int + z_int) % 2 == 0;
    return is_even ? even_tex->value(u, v, p) : odd_tex->value(u, v, p);
  }

private:
  shared_ptr<texture> even_tex;
  shared_ptr<texture> odd_tex;
  double inv_scale;
};

#endif
