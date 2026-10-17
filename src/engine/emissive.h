#ifndef EMISSIVE_H
#define EMISSIVE_H

#include "material.h"

/**
 * @brief Diffuse emitter with constant radiance.
 *
 * Emits from both faces by default, which matches how the scene files were
 * authored (OBJ light quads are not consistently oriented). The emitter
 * absorbs everything that reaches it.
 */
class emissive : public material {
public:
  emissive(const color &c) : pcolor(c) {}
  emissive(const color &c, double intensity) : pcolor(c * intensity) {}

  bool is_emissive() const override { return true; }
  bool scatters() const override { return false; }
  bool has_smooth_lobes() const override { return false; }

  color emitted(const hit_record &rec, const vec3 &wo) const override {
    (void)wo;
    return (two_sided || rec.front_face) ? pcolor : color(0, 0, 0);
  }

  color eval(const hit_record &, const vec3 &, const vec3 &) const override {
    return color(0, 0, 0);
  }
  double pdf(const hit_record &, const vec3 &, const vec3 &) const override {
    return 0.0;
  }
  bool sample(const hit_record &, const vec3 &, double, double, double,
              bsdf_sample &) const override {
    return false;
  }

  const color &radiance() const { return pcolor; }

public:
  color pcolor;
  bool two_sided = true;
};

#endif
