#ifndef PROGRESSIVE_H
#define PROGRESSIVE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "engine/render_runner.h"
#include "eval/metrics.h"
#include "util/image_io.h"

class world;

/**
 * @brief Progressive rendering for convergence and equal-time experiments.
 *
 * Renders in passes whose sample counts double the running total (1, 1, 2,
 * 4, ... spp), giving log-spaced points on an error-vs-time curve. With a
 * time budget, the last passes shrink to fit it, sized from the measured cost
 * per sample of the previous pass.
 *
 * Timing covers rendering only: scene loading, BVH construction and metric
 * evaluation are excluded. Because each sample's random stream depends only on
 * its index, the final image equals a single-pass render at the same spp.
 */
namespace eval {

struct pass_record {
  int pass = 0;
  int pass_spp = 0;
  long long spp = 0;          // cumulative samples per pixel
  double pass_seconds = 0.0;
  double total_seconds = 0.0; // cumulative render time
  uint64_t camera_rays = 0;   // cumulative
  uint64_t bounce_rays = 0;
  uint64_t shadow_rays = 0;
  bool has_metrics = false;
  metrics::report error;      // against the reference, if one was given
};

struct progressive_settings {
  double time_budget = 0.0;  // seconds of render time; 0 = no limit
  long long max_spp = 0;     // 0 = no limit (at least one limit is required)
  unsigned int threads = 1;
  int tile_size = 32;
  const image_rgb *reference = nullptr;
  const image_rgb *reference_variance = nullptr;
  bool flip = true;          // FLIP for every pass (only with a reference)
  metrics::flip_options flip_options;
  std::function<void(const pass_record &)> on_pass;
};

struct progressive_result {
  render::RenderBuffers buffers;
  std::vector<pass_record> passes;
};

// Throws std::invalid_argument if no limit is set or the reference size
// does not match the image.
progressive_result render_progressive(world &scene,
                                      const progressive_settings &settings);

// CSV with one row per pass. `scene` and `method` label the run.
std::string csv_header();
std::string csv_row(const pass_record &p, const std::string &scene,
                    const std::string &method);

} // namespace eval

#endif
