#include "eval/metrics.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

// FLIP.h defines global Min/Max macros; keep it confined to this file.
#include "3rdParty/flip/FLIP.h"

namespace metrics {

report compare(const image_rgb &test, const image_rgb &reference,
               const image_rgb *reference_variance, bool with_flip,
               const flip_options &fo, double rel_eps) {
  if (test.width != reference.width || test.height != reference.height)
    throw std::invalid_argument("image sizes differ");
  if (reference_variance && (reference_variance->width != reference.width ||
                             reference_variance->height != reference.height))
    throw std::invalid_argument("reference variance size differs");

  report r;
  r.pixels = reference.pixel_count();
  double se = 0.0, rel = 0.0, ape = 0.0, var_abs = 0.0, var_rel = 0.0;
  size_t n = 0; // valid channel samples
  for (size_t p = 0; p < r.pixels; ++p) {
    bool finite = true;
    for (int c = 0; c < 3; ++c)
      finite = finite && std::isfinite(test.data[3 * p + c]);
    if (!finite) {
      ++r.invalid_pixels;
      continue;
    }
    for (int c = 0; c < 3; ++c) {
      const double t = test.data[3 * p + c];
      const double ref = reference.data[3 * p + c];
      const double d = t - ref;
      const double rel_norm = ref * ref + rel_eps;
      se += d * d;
      rel += d * d / rel_norm;
      ape += std::fabs(d) / (std::fabs(ref) + rel_eps);
      if (reference_variance) {
        const double v = reference_variance->data[3 * p + c];
        var_abs += v;
        var_rel += v / rel_norm;
      }
      ++n;
    }
  }
  if (n > 0) {
    r.mse = se / n;
    r.relmse = rel / n;
    r.mape = ape / n;
  }
  r.rmse = std::sqrt(r.mse);
  if (reference_variance && n > 0) {
    r.corrected = true;
    r.mse_corrected = r.mse - var_abs / n;
    r.relmse_corrected = r.relmse - var_rel / n;
  }
  if (with_flip)
    r.flip = flip(test, reference, fo);
  return r;
}

double flip(const image_rgb &test, const image_rgb &reference,
            const flip_options &fo, image_rgb *heatmap) {
  if (test.width != reference.width || test.height != reference.height)
    throw std::invalid_argument("image sizes differ");

  // FLIP needs finite input; map NaN/Inf to 0 so they count as errors
  // instead of poisoning the whole map.
  std::vector<float> t(test.data), ref(reference.data);
  for (float &v : t)
    v = std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
  for (float &v : ref)
    v = std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
  if (!fo.hdr) {
    for (float &v : t)
      v = std::min(v, 1.0f);
    for (float &v : ref)
      v = std::min(v, 1.0f);
  }

  FLIP::Parameters params;
  if (fo.ppd > 0.0)
    params.PPD = static_cast<float>(fo.ppd);
  float mean = -1.0f;
  float *map = nullptr;
  FLIP::evaluate(ref.data(), t.data(), reference.width, reference.height,
                 fo.hdr, params, /*applyMagmaMapToOutput=*/heatmap != nullptr,
                 /*computeMeanFLIPError=*/true, mean, &map);
  if (heatmap) {
    *heatmap = image_rgb(reference.width, reference.height);
    std::copy(map, map + heatmap->data.size(), heatmap->data.begin());
  }
  delete[] map;
  return mean;
}

std::string to_json(const report &r) {
  std::ostringstream os;
  os.precision(10);
  os << "{\"pixels\": " << r.pixels << ", \"invalid_pixels\": "
     << r.invalid_pixels << ", \"mse\": " << r.mse << ", \"rmse\": " << r.rmse
     << ", \"relmse\": " << r.relmse << ", \"mape\": " << r.mape;
  if (r.corrected)
    os << ", \"mse_corrected\": " << r.mse_corrected
       << ", \"relmse_corrected\": " << r.relmse_corrected;
  if (r.flip >= 0.0)
    os << ", \"flip\": " << r.flip;
  os << "}";
  return os.str();
}

} // namespace metrics
