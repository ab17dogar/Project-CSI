#ifndef METAL_H
#define METAL_H

#include "pbr_material.h"

/**
 * @brief Conductor (XML type "Metal"): GGX with F0 = albedo.
 *
 * The Ray Tracing in One Weekend "fuzz" perturbation has no closed-form
 * pdf, so it cannot be combined with light sampling. Fuzz is reinterpreted
 * as GGX roughness: fuzz 0 is a perfect mirror, larger values blur the
 * reflection in a comparable way.
 */
class metal : public pbr_material {
public:
  metal(const color &a, double f)
      : pbr_material(a, 1.0f, static_cast<float>(f < 1 ? f : 1)), fuzz(f < 1 ? f : 1) {}

public:
  double fuzz;
};

#endif
