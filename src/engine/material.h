#ifndef MATERIAL_H
#define MATERIAL_H

#include "../util/ray.h"
#include "hittable.h"
#include "onb.h"

/**
 * @brief Result of importance-sampling a BSDF.
 *
 * All directions are in world space, unit length and point away from the
 * surface (wo toward the viewer, wi toward the light), following pbrt.
 */
struct bsdf_sample {
  vec3 wi;
  // f(wo, wi) * |cos theta_i| / pdf, ready to multiply into path throughput.
  // For delta lobes the Dirac deltas in f and pdf cancel analytically.
  color weight{0, 0, 0};
  // Solid-angle density with which the material's non-delta lobes generate
  // wi. Used as the BSDF-side MIS pdf. Meaningless when is_delta is set.
  double pdf = 0.0;
  bool is_delta = false;
  bool is_transmission = false;
  // Relative index of refraction crossed (1 for reflection). Tracked by the
  // integrator so Russian roulette is not skewed by the 1/eta^2 radiance
  // scaling inside dense media.
  double eta = 1.0;
};

/**
 * @brief Base class for all surface and volume scattering models.
 *
 * The previous interface was a single `scatter()` call returning a sampling
 * weight. That cannot support next event estimation or MIS, which need the
 * BSDF value and pdf for an arbitrary light direction. Every material now
 * implements the standard triple:
 *
 *   sample(wo)    -> wi, f*cos/pdf, pdf     (BSDF sampling)
 *   eval(wo, wi)  -> f(wo, wi)              (light sampling)
 *   pdf(wo, wi)   -> p(wi | wo)             (MIS weights)
 *
 * The test suite checks that the three agree for every material.
 */
class material {
public:
  virtual ~material() = default;

  // Emitted radiance leaving the surface toward wo.
  virtual color emitted(const hit_record &rec, const vec3 &wo) const {
    (void)rec;
    (void)wo;
    return color(0, 0, 0);
  }
  virtual bool is_emissive() const { return false; }

  // False for pure emitters: the path ends at them.
  virtual bool scatters() const { return true; }

  // True if any lobe has a finite density, i.e. light sampling can help.
  // Perfect mirrors and smooth glass return false.
  virtual bool has_smooth_lobes() const { return true; }

  // Phase functions (participating media) have no cosine foreshortening.
  virtual bool is_phase_function() const { return false; }

  // f(wo, wi) over the non-delta lobes, without the cosine factor.
  virtual color eval(const hit_record &rec, const vec3 &wo,
                     const vec3 &wi) const = 0;

  // Solid-angle density of sample() producing wi via its non-delta lobes.
  virtual double pdf(const hit_record &rec, const vec3 &wo,
                     const vec3 &wi) const = 0;

  // uc picks a lobe; (u1, u2) sample a direction within it.
  virtual bool sample(const hit_record &rec, const vec3 &wo, double uc,
                      double u1, double u2, bsdf_sample &out) const = 0;
};

// Cosine of the angle between w and the shading normal (signed).
inline double cos_theta(const hit_record &rec, const vec3 &w) {
  return dot(w, rec.normal);
}

#endif
