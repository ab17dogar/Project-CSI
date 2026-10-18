#ifndef GAUSSIAN_FIELD_H
#define GAUSSIAN_FIELD_H

#include <string>
#include <vector>

#include "aabb.h"
#include "light.h"
#include "sampling.h"
#include "util/ray.h"
#include "util/vec3.h"

/**
 * @brief A 3D Gaussian Splatting capture as an emitting, absorbing medium.
 *
 * Radiance model: the particle model of 3DGRT (Moenne-Loccoz et al. 2024).
 * Gaussian i meets a ray at its point of maximum response, t_i, with opacity
 * alpha_i = o_i exp(-d_i^2 / 2), d_i the Mahalanobis distance of the ray to
 * the mean there, and view-dependent radiance c_i(direction) from spherical
 * harmonics. Along a segment the field contributes
 *
 *     L = sum_i T_i alpha_i c_i,   T_i = prod_{j: t_j < t_i} (1 - alpha_j)
 *
 * and lets through T = prod_i (1 - alpha_i) of the light behind it. This is
 * what a trained 3DGS scene shows when ray traced; it is evaluated exactly
 * (no early termination), so estimators built on it are unbiased for this
 * model. The field emits and absorbs; it does not scatter.
 *
 * Conventions follow the reference implementation: opacity is stored as a
 * logit, scales as logs, rotation as a quaternion (w, x, y, z); radiance is
 * SH + 0.5, clamped at 0; opacities are clamped to 0.99 and Gaussians whose
 * response cannot reach 1/255 are dropped.
 */
class gaussian_field {
public:
  struct gaussian {
    float mean[3];
    float whiten[9]; // row-major M = S^-1 R^T: local = M (x - mean)
    float opacity;   // activated, <= 0.99
    float sh[48];    // [coefficient][channel], coefficients 0..15
  };

  static constexpr double kMaxOpacity = 0.99;
  static constexpr double kMinAlpha = 1.0 / 255.0;

  // Reads a binary little-endian 3DGS .ply. Returns false and sets *error on
  // failure.
  bool load_ply(const std::string &path, std::string *error = nullptr);

  // Adds one Gaussian from activated parameters (scales, unit or non-unit
  // quaternion w, x, y, z, opacity in [0, 1]). sh holds (degree + 1)^2 RGB
  // triples, coefficient-major. Returns false if it can never contribute.
  bool add(const vec3 &mean, const vec3 &scale, const double quat[4],
           double opacity, const float *sh, int degree);

  // Places the field in the scene: x_world = position + scale * R x_local,
  // R = Rx Ry Rz (degrees), the composition meshes use.
  void set_transform(const vec3 &position, double scale,
                     const vec3 &rotation_degrees);

  // Builds the acceleration structure; call after the last add / load.
  void build();

  /**
   * MIS weights for the field's emission on a BSDF-sampled ray. Light
   * sampling treats every Gaussian as its own light (sample_emission), so
   * each Gaussian i along the ray is weighted against the BSDF density with
   * its own light density P_field * P_i * p_i(direction).
   */
  struct mis_context {
    point3 origin;           // the shading point the ray was sampled from
    double bsdf_pdf = 0.0;   // solid-angle density of the ray's direction
    double light_pmf = 0.0;  // probability of picking the field as the light
    bool nee_only = false;   // light sampling only: weight 0 where it covers
  };

  // Emission and transmittance along r for t in (t_min, t_max). With mis,
  // each Gaussian's emission is MIS-weighted (transmittance is not).
  void trace(const ray &r, double t_min, double t_max, color &radiance,
             double &transmittance, const mis_context *mis = nullptr) const;
  // Transmittance, optionally leaving out one Gaussian (the one a light
  // sample was drawn from: it emits the light, it does not block it).
  double transmittance(const ray &r, double t_min, double t_max,
                       int exclude = -1) const;

  /**
   * Light sampling: picks Gaussian i in proportion to its emitted power,
   * draws a point from its 3D distribution and connects x to it. The
   * direction density is the closed-form line integral
   * p_i(w) = integral over t > 0 of N(x + t w; mu_i, Sigma_i) t^2 dt.
   * Fills out.wi, dist (to the Gaussian's maximum response along wi), p,
   * li = alpha_i c_i (the term's radiance without the transmittance in
   * front of it), pdf = P_i p_i(wi) and field_index. False if the sampled
   * direction meets the Gaussian behind x or below the 1/255 cut-off.
   */
  bool sample_emission(const point3 &x, double u_select, const double u[4],
                       light_sample &out) const;
  // P_i p_i(w) for a unit world direction w from x.
  double emission_pdf(int i, const point3 &x, const vec3 &w) const;

  /**
   * How light sampling picks a Gaussian. power: in proportion to emitted
   * power, the same everywhere. tree: a light hierarchy over the acceleration
   * structure; at a shading point each node is weighted by its power over
   * its squared distance (clamped by its size), down to one Gaussian, so
   * nearby emitters are preferred. P_i then depends on the shading point.
   */
  enum class selection { power, tree };
  void set_selection(selection s) { selection_ = s; }
  selection selection_mode() const { return selection_; }
  // Picks a Gaussian for light sampling at world point x; pmf = P_i(x).
  // Returns -1 if nothing emits.
  int select_gaussian(const point3 &x, double u, double &pmf) const;
  // P_i(x) of that choice.
  double select_pmf(int i, const point3 &x) const;
  // Luminance of the total emitted power (light::power units).
  double power() const { return total_power_; }

  // Radiance of one Gaussian toward a direction in the field's frame:
  // SH + 0.5, clamped at 0.
  static color eval_sh(int degree, const float *sh, const vec3 &dir);

  size_t size() const { return g_.size(); }
  int sh_degree() const { return degree_; }
  const std::vector<gaussian> &gaussians() const { return g_; }
  // Bounds in world space (valid after build()).
  aabb world_bounds() const;

private:
  struct hit {
    double t;
    double alpha;
    int index;
  };
  struct node {
    float lo[3], hi[3];
    int first; // child index for inner nodes, first primitive for leaves
    int count; // 0 for inner nodes
  };

  void to_local(const ray &r, point3 &o, vec3 &d) const;
  point3 point_to_local(const point3 &x) const;
  // Selection in the field's frame (tree importances use local distances).
  int select_local(const point3 &x, double u, double &pmf) const;
  double select_pmf_local(int i, const point3 &x) const;
  double node_importance(int node, const point3 &x) const;
  double leaf_weight(int g, const point3 &x) const;
  // p_i for a local origin and unit local direction.
  double direction_pdf_local(const gaussian &g, const point3 &o, const vec3 &w) const;
  // Every Gaussian whose maximum response along the local ray lies in
  // (t_min, t_max) with alpha >= kMinAlpha, in no particular order.
  void collect(const point3 &o, const vec3 &d, double t_min, double t_max,
               std::vector<hit> &out) const;
  int build_node(int first, int count, int depth);

  std::vector<gaussian> g_;
  std::vector<float> box_lo_, box_hi_; // per Gaussian, local frame
  std::vector<node> nodes_;
  std::vector<int> order_;
  int degree_ = 0;

  // Light selection, built with the acceleration structure.
  sampling::distribution1d select_;
  double total_power_ = 0.0;
  selection selection_ = selection::power;
  std::vector<double> g_power_;    // per Gaussian, local units
  std::vector<double> node_power_; // per node: sum over its Gaussians
  std::vector<int> parent_;        // per node (-1 for the root)
  std::vector<int> leaf_of_;       // per Gaussian: its leaf node

  // World -> local: x_local = rot_ (x - position_) / scale_.
  double rot_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // R^T, row-major
  vec3 position_{0, 0, 0};
  double scale_ = 1.0;
};

/**
 * @brief The field as a light for next event estimation. Not hittable and
 * not infinite: the integrator weights its emission on BSDF rays itself
 * (gaussian_field::mis_context), so pdf_li is never used.
 */
class gaussian_field_light : public light {
public:
  explicit gaussian_field_light(const gaussian_field *field)
      : light(kind::area), field_(field) {}
  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &, const vec3 &, const hit_record *) const override {
    return 0.0;
  }
  double power() const override { return field_->power(); }

private:
  const gaussian_field *field_;
};

#endif
