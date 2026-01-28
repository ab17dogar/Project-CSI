#ifndef NOISE_TEXTURE_H
#define NOISE_TEXTURE_H

#include "perlin.h"
#include "texture.h"

/**
 * @brief Noise-based procedural texture using Perlin noise
 *
 * Can create various effects:
 * - Basic noise (scale only)
 * - Turbulent noise (multi-octave)
 * - Marble effect (using sine with turbulence phase)
 */
class noise_texture : public texture {
public:
  noise_texture() : scale(1.0) {}
  noise_texture(double sc) : scale(sc) {}

  virtual color value(double u, double v, const point3 &p) const override {
    (void)u;
    (void)v;
    // Marble-like effect using sine with turbulence
    return color(1, 1, 1) * 0.5 * (1 + sin(scale * p.z() + 10 * noise.turb(p)));
  }

private:
  perlin noise;
  double scale;
};

/**
 * @brief Turbulent noise texture for clouds/smoke effects
 */
class turb_texture : public texture {
public:
  turb_texture() : scale(1.0) {}
  turb_texture(double sc) : scale(sc) {}

  virtual color value(double u, double v, const point3 &p) const override {
    (void)u;
    (void)v;
    return color(1, 1, 1) * noise.turb(scale * p);
  }

private:
  perlin noise;
  double scale;
};

#endif
