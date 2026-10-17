#ifndef METRICS_H
#define METRICS_H

#include <cstddef>
#include <string>

#include "util/image_io.h"

/**
 * @brief Image error metrics for comparing renders against references.
 *
 * Definitions (all averaged over pixels and the three color channels, with
 * t the test image and r the reference):
 *
 *   MSE     mean (t - r)^2
 *   relMSE  mean (t - r)^2 / (r^2 + eps)      eps = 0.01 (Rousselle et al. 2011)
 *   MAPE    mean |t - r| / (r + eps)
 *   FLIP    NVIDIA HDR-FLIP (or LDR-FLIP) mean error, in [0, 1]
 *
 * A reference rendered with finitely many samples is itself noisy, and
 * E[(t - r)^2] = E[(t - mu)^2] + Var(r) when t and r are independent and
 * unbiased. Passing the reference's per-pixel variance of the mean removes
 * that term: *_corrected = metric - mean(Var(r) / normalizer).
 */
namespace metrics {

struct flip_options {
  bool hdr = true;   // HDR-FLIP for radiance; LDR-FLIP expects [0,1] input
  double ppd = 0.0;  // pixels per degree; 0 = FLIP's default (~67)
};

struct report {
  size_t pixels = 0;
  size_t invalid_pixels = 0; // non-finite test pixels (excluded)
  double mse = 0.0;
  double rmse = 0.0;
  double relmse = 0.0;
  double mape = 0.0;
  bool corrected = false;    // reference variance was supplied
  double mse_corrected = 0.0;
  double relmse_corrected = 0.0;
  double flip = -1.0;        // < 0: not computed
};

// Throws std::invalid_argument when the images differ in size.
report compare(const image_rgb &test, const image_rgb &reference,
               const image_rgb *reference_variance = nullptr,
               bool with_flip = false, const flip_options &fo = {},
               double rel_eps = 1e-2);

// Mean FLIP error. If `heatmap` is non-null it receives the magma-mapped
// error image (display-encoded, write with write_png_display).
double flip(const image_rgb &test, const image_rgb &reference,
            const flip_options &fo = {}, image_rgb *heatmap = nullptr);

// One-line JSON object with every field of the report.
std::string to_json(const report &r);

} // namespace metrics

#endif
