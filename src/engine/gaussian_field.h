#ifndef GAUSSIAN_FIELD_H
#define GAUSSIAN_FIELD_H

#include <string>
#include <vector>

#include "aabb.h"
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

  // Emission and transmittance along r for t in (t_min, t_max).
  void trace(const ray &r, double t_min, double t_max, color &radiance,
             double &transmittance) const;
  double transmittance(const ray &r, double t_min, double t_max) const;

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

  // World -> local: x_local = rot_ (x - position_) / scale_.
  double rot_[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1}; // R^T, row-major
  vec3 position_{0, 0, 0};
  double scale_ = 1.0;
};

#endif
