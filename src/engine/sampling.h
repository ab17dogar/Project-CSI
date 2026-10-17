#ifndef SAMPLING_H
#define SAMPLING_H

#include "../util/vec3.h"
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * @brief Sample warping functions, discrete/continuous distributions and MIS
 * heuristics.
 *
 * Every warp takes explicit uniform numbers in [0,1)^2 instead of drawing
 * them internally. That keeps the mapping deterministic and testable (the
 * test suite checks each warp against its pdf), and leaves room for
 * stratified or low-discrepancy samplers later.
 *
 * Local directions use the convention z = surface normal.
 */
namespace sampling {

// Shirley-Chiu concentric map: uniform on the unit disk, low distortion.
inline void square_to_concentric_disk(double u1, double u2, double &x,
                                      double &y) {
  double ox = 2.0 * u1 - 1.0;
  double oy = 2.0 * u2 - 1.0;
  if (ox == 0.0 && oy == 0.0) {
    x = y = 0.0;
    return;
  }
  double r, theta;
  if (std::fabs(ox) > std::fabs(oy)) {
    r = ox;
    theta = (PI / 4.0) * (oy / ox);
  } else {
    r = oy;
    theta = (PI / 2.0) - (PI / 4.0) * (ox / oy);
  }
  x = r * std::cos(theta);
  y = r * std::sin(theta);
}

// Malley's method: project the concentric disk up to the hemisphere.
inline vec3 square_to_cosine_hemisphere(double u1, double u2) {
  double x, y;
  square_to_concentric_disk(u1, u2, x, y);
  double z = std::sqrt(std::max(0.0, 1.0 - x * x - y * y));
  return vec3(x, y, z);
}

inline double cosine_hemisphere_pdf(double cos_theta) {
  return cos_theta > 0.0 ? cos_theta * INV_PI : 0.0;
}

inline vec3 square_to_uniform_sphere(double u1, double u2) {
  double z = 1.0 - 2.0 * u1;
  double r = std::sqrt(std::max(0.0, 1.0 - z * z));
  double phi = 2.0 * PI * u2;
  return vec3(r * std::cos(phi), r * std::sin(phi), z);
}

inline double uniform_sphere_pdf() { return INV_4PI; }

/**
 * Uniform direction inside the cone around +z with half-angle theta_max.
 * Takes (1 - cos theta_max) rather than cos theta_max: for small, distant
 * spheres 1 - cos is ~1e-10 and forming it by subtraction would lose every
 * significant digit.
 */
inline vec3 square_to_uniform_cone(double u1, double u2,
                                   double one_minus_cos_max) {
  double cos_theta = 1.0 - u1 * one_minus_cos_max;
  double sin2 = u1 * one_minus_cos_max * (2.0 - u1 * one_minus_cos_max);
  double sin_theta = std::sqrt(std::max(0.0, sin2));
  double phi = 2.0 * PI * u2;
  return vec3(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
}

inline double uniform_cone_pdf(double one_minus_cos_max) {
  return 1.0 / (2.0 * PI * one_minus_cos_max);
}

// Uniform barycentrics (b0, b1) on a triangle; b2 = 1 - b0 - b1.
inline void square_to_triangle(double u1, double u2, double &b0, double &b1) {
  double su = std::sqrt(u1);
  b0 = 1.0 - su;
  b1 = u2 * su;
}

/**
 * Power heuristic with beta = 2 (Veach 1997) for one sample from each of two
 * strategies. An infinite pdf (a delta strategy) takes the full weight.
 */
inline double power_heuristic(double f_pdf, double g_pdf) {
  if (std::isinf(f_pdf))
    return 1.0;
  if (std::isinf(g_pdf))
    return 0.0;
  double f = f_pdf * f_pdf;
  double g = g_pdf * g_pdf;
  if (f + g <= 0.0)
    return 0.0;
  return f / (f + g);
}

inline double balance_heuristic(double f_pdf, double g_pdf) {
  if (f_pdf + g_pdf <= 0.0)
    return 0.0;
  return f_pdf / (f_pdf + g_pdf);
}

/**
 * @brief Piecewise-constant 1D distribution over [0,1) with n cells.
 *
 * Used both as a discrete distribution (light selection by power) and as a
 * continuous one (the marginal/conditional of an environment map).
 */
class distribution1d {
public:
  distribution1d() = default;

  explicit distribution1d(const std::vector<double> &values) { build(values); }

  void build(const std::vector<double> &values) {
    func.assign(values.begin(), values.end());
    for (double &f : func)
      f = (std::isfinite(f) && f > 0.0) ? f : 0.0;
    const size_t n = func.size();
    cdf.assign(n + 1, 0.0);
    for (size_t i = 0; i < n; ++i)
      cdf[i + 1] = cdf[i] + func[i] / static_cast<double>(n);
    func_int = cdf[n];
    if (func_int <= 0.0) {
      // Degenerate input: fall back to uniform so sampling stays defined.
      for (size_t i = 1; i <= n; ++i)
        cdf[i] = static_cast<double>(i) / static_cast<double>(n);
    } else {
      for (size_t i = 1; i <= n; ++i)
        cdf[i] /= func_int;
    }
    cdf[n] = 1.0;
  }

  size_t count() const { return func.size(); }
  double integral() const { return func_int; }

  // Index of the cell containing u (largest i with cdf[i] <= u).
  size_t find_cell(double u) const {
    auto it = std::upper_bound(cdf.begin(), cdf.end(), u);
    size_t i = static_cast<size_t>(std::max<std::ptrdiff_t>(
        0, static_cast<std::ptrdiff_t>(it - cdf.begin()) - 1));
    return std::min(i, func.size() - 1);
  }

  // Continuous sample in [0,1); pdf is with respect to [0,1).
  double sample_continuous(double u, double &pdf, size_t *cell = nullptr) const {
    size_t i = find_cell(u);
    if (cell)
      *cell = i;
    double width = cdf[i + 1] - cdf[i];
    double du = width > 0.0 ? (u - cdf[i]) / width : 0.0;
    pdf = func_int > 0.0 ? func[i] / func_int : 1.0;
    double x = (static_cast<double>(i) + du) / static_cast<double>(count());
    return std::min(x, ONE_MINUS_EPSILON);
  }

  size_t sample_discrete(double u, double &pmf) const {
    size_t i = find_cell(u);
    pmf = discrete_pmf(i);
    return i;
  }

  double discrete_pmf(size_t i) const {
    if (i >= func.size())
      return 0.0;
    if (func_int <= 0.0)
      return 1.0 / static_cast<double>(func.size());
    return func[i] / (func_int * static_cast<double>(func.size()));
  }

  // Continuous density at x in [0,1).
  double pdf(double x) const {
    if (func.empty())
      return 0.0;
    size_t i = std::min(func.size() - 1,
                        static_cast<size_t>(std::max(0.0, x) * func.size()));
    return func_int > 0.0 ? func[i] / func_int : 1.0;
  }

  std::vector<double> func;
  std::vector<double> cdf;
  double func_int = 0.0;
};

/**
 * @brief Piecewise-constant 2D distribution on [0,1)^2, sampled as a marginal
 * over rows (v) followed by a conditional over columns (u).
 */
class distribution2d {
public:
  distribution2d() = default;

  // values is row-major with `height` rows of `width` entries.
  void build(const std::vector<double> &values, int width, int height) {
    nu = width;
    nv = height;
    conditional.clear();
    conditional.reserve(static_cast<size_t>(height));
    std::vector<double> marginal_func(static_cast<size_t>(height));
    std::vector<double> row(static_cast<size_t>(width));
    for (int v = 0; v < height; ++v) {
      for (int u = 0; u < width; ++u)
        row[static_cast<size_t>(u)] =
            values[static_cast<size_t>(v) * width + u];
      conditional.emplace_back(row);
      marginal_func[static_cast<size_t>(v)] = conditional.back().integral();
    }
    marginal.build(marginal_func);
  }

  bool valid() const { return nu > 0 && nv > 0; }

  // Returns (u, v) in [0,1)^2 and the density with respect to [0,1)^2.
  void sample(double u1, double u2, double &u, double &v, double &pdf) const {
    double pdf_v, pdf_u;
    size_t row = 0;
    v = marginal.sample_continuous(u2, pdf_v, &row);
    u = conditional[row].sample_continuous(u1, pdf_u);
    pdf = pdf_u * pdf_v;
  }

  double pdf(double u, double v) const {
    int iu = std::clamp(static_cast<int>(u * nu), 0, nu - 1);
    int iv = std::clamp(static_cast<int>(v * nv), 0, nv - 1);
    if (marginal.integral() <= 0.0)
      return 1.0;
    return conditional[static_cast<size_t>(iv)].func[static_cast<size_t>(iu)] /
           marginal.integral();
  }

  int nu = 0;
  int nv = 0;
  std::vector<distribution1d> conditional;
  distribution1d marginal;
};

} // namespace sampling

#endif
