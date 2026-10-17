#ifndef GGX_MATERIAL_H
#define GGX_MATERIAL_H

#include "pbr_material.h"

/**
 * @brief GGX microfacet material (XML type "GGX").
 *
 * Kept as its own type for scene compatibility. It is the same
 * metallic-roughness model as pbr_material; the earlier standalone version
 * sampled the plain NDF, never divided by its pdf and clamped the result,
 * so it was neither correct nor consistent with light sampling.
 */
class ggx_material : public pbr_material {
public:
  ggx_material(const color &a, double rough, double metal = 0.0)
      : pbr_material(a, static_cast<float>(metal), static_cast<float>(rough)) {}
};

#endif
