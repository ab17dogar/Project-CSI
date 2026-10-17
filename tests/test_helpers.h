#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <memory>

#include "engine/config.h"
#include "engine/integrator.h"
#include "engine/world.h"
#include "util/util.h"
#include "util/vec3.h"

namespace th {

// A world with a config but no camera, sun or sky: lights come only from
// what the test adds.
inline std::shared_ptr<world> make_world(int max_depth = 1) {
  auto w = std::make_shared<world>();
  w->pconfig = std::make_shared<config>();
  w->pconfig->IMAGE_WIDTH = 8;
  w->pconfig->IMAGE_HEIGHT = 8;
  w->pconfig->ASPECT_RATIO = 1.0;
  w->pconfig->SAMPLES_PER_PIXEL = 1;
  w->pconfig->MAX_DEPTH = max_depth;
  w->pconfig->acceleration = AccelerationMethod::BVH;
  w->pconfig->enableDenoiser = false;
  return w;
}

struct estimate {
  color mean{0, 0, 0};
  color std_error{0, 0, 0};
};

// Monte Carlo estimate of the radiance along one ray, with its standard
// error. Every sample has its own deterministic random stream.
inline estimate estimate_radiance(const world &w, const ray &r,
                                  const path_tracer_settings &s, int n,
                                  uint64_t seed = 1) {
  // Welford's algorithm: the naive E[x^2] - E[x]^2 cancels catastrophically
  // for low-variance estimators.
  double mean[3] = {0, 0, 0};
  double m2[3] = {0, 0, 0};
  for (int i = 0; i < n; ++i) {
    seed_thread_rng(static_cast<uint64_t>(i), 0, seed);
    const color c = trace_path(r, w, s);
    for (int k = 0; k < 3; ++k) {
      const double delta = c[k] - mean[k];
      mean[k] += delta / (i + 1);
      m2[k] += delta * (c[k] - mean[k]);
    }
  }
  estimate e;
  for (int k = 0; k < 3; ++k) {
    e.mean[k] = mean[k];
    e.std_error[k] = n > 1 ? std::sqrt(m2[k] / (n - 1) / n) : 0.0;
  }
  return e;
}

inline const char *strategy_name(LightStrategy s) {
  switch (s) {
  case LightStrategy::MIS:
    return "mis";
  case LightStrategy::BSDF:
    return "bsdf";
  case LightStrategy::NEE:
    return "nee";
  }
  return "?";
}

} // namespace th

#endif
