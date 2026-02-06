#ifndef SCATTER_RECORD_H
#define SCATTER_RECORD_H

#include "../util/vec3.h"
#include "pdf.h"
#include <memory>

/**
 * @brief Record of a material scatter event
 *
 * Used for importance-sampled materials where the scattered
 * ray direction is determined by a PDF rather than directly
 * by the material.
 *
 * For specular materials (metal, dielectric), the scattered
 * ray is deterministic, so is_specular is true and skip_pdf
 * indicates we should use the specular_ray directly.
 */
struct scatter_record {
  // For specular (deterministic) reflections
  ray specular_ray;
  bool is_specular;

  // For diffuse (PDF-based) scattering
  color attenuation;
  shared_ptr<pdf> pdf_ptr;

  // Skip PDF evaluation (for specular materials)
  bool skip_pdf;
};

#endif
