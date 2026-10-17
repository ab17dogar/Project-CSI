#ifndef PBR_MATERIAL_H
#define PBR_MATERIAL_H

#include "../util/vec3.h"
#include "hittable.h"
#include "material.h"
#include "texture.h"
#include <cmath>
#include <memory>

/**
 * @brief Physically-Based Rendering (PBR) material, metallic-roughness model.
 *
 * Same parameterization as glTF 2.0 / Disney "principled": base color,
 * metallic, roughness (alpha = roughness^2).
 *
 *   f = (1 - m) * f_dielectric + m * f_metal
 *   f_metal      = GGX(F0 = base)
 *   f_dielectric = GGX(F0 = 0.04) + diffuse
 *
 * The specular lobes are Cook-Torrance GGX with height-correlated Smith
 * masking and Schlick Fresnel, sampled with the visible-normal distribution.
 *
 * The diffuse lobe deliberately differs from the glTF reference
 * `fresnel_mix`. That one weights diffuse by (1 - F(v.h)), which is neither
 * reciprocal nor energy conserving: a white smooth dielectric reflects up to
 * ~2x the incident energy at grazing angles. Here diffuse uses the coupling
 * of Kelemen & Szirmay-Kalos (2001):
 *
 *   f_diffuse = base/pi * (1 - E(wo)) (1 - E(wi)) / (1 - E_avg)
 *
 * where E is the directional albedo of the dielectric specular lobe
 * (tabulated at startup). It is symmetric in wo and wi, and the total albedo
 * is E(wo) + base * (1 - E(wo)) <= 1.
 *
 * Roughness below 0.01 is treated as perfectly smooth: the specular lobe
 * becomes a Dirac delta (mirror), handled by BSDF sampling only.
 */
class pbr_material : public material {
public:
  pbr_material(shared_ptr<texture> albedo_tex, float metallic = 0.0f,
               float roughness = 0.5f);
  pbr_material(const color &c, float metallic = 0.0f, float roughness = 0.5f);

  color eval(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override;
  double pdf(const hit_record &rec, const vec3 &wo,
             const vec3 &wi) const override;
  bool sample(const hit_record &rec, const vec3 &wo, double uc, double u1,
              double u2, bsdf_sample &out) const override;
  bool has_smooth_lobes() const override { return !smooth || metallic < 1.0; }

  bool is_smooth() const { return smooth; }
  double alpha() const { return alpha_; }

  // Directional albedo of the F0 = 0.04 GGX lobe (smooth: Schlick Fresnel).
  static double dielectric_specular_albedo(double cos_theta, double roughness);
  static double dielectric_specular_average(double roughness);

public:
  shared_ptr<texture> albedo;
  double metallic;
  double roughness;

private:
  struct lobe_setup {
    color base;
    color f0;
    double spec_weight; // probability of sampling the specular lobe
  };
  lobe_setup setup(const hit_record &rec, double cos_o) const;
  color eval_local(const lobe_setup &s, const vec3 &wo, const vec3 &wi) const;
  double pdf_local(const lobe_setup &s, const vec3 &wo, const vec3 &wi) const;
  color diffuse_local(const lobe_setup &s, double cos_o, double cos_i) const;

  double alpha_;
  bool smooth;
};

#endif
