#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "util/image_io.h"
#include "util/vec3.h"

class ray;
class world;

namespace render {

struct TileProgressStats {
  size_t tilesDone{0};
  size_t totalTiles{0};
  double avgTileMs{0.0};
  double estRemainingMs{0.0};
};

using TileCallback =
    std::function<void(const std::vector<color> &bitmap, int width, int height,
                       const TileProgressStats &stats)>;

/**
 * @brief Per-pixel accumulation of path samples.
 *
 * Holds both the running sum and the sum of squares, so the image and the
 * per-pixel variance of its mean are available after any number of passes.
 * Pixel (x, y) with y = 0 at the top is at index y * width + x.
 */
struct RenderBuffers {
  int width = 0;
  int height = 0;
  long long spp = 0; // samples per pixel accumulated so far
  std::vector<color> sum;
  std::vector<color> sum_sq;

  void reset(int w, int h);
  image_rgb mean() const;
  // Estimated variance of each pixel's mean (sample variance / spp).
  image_rgb variance_of_mean() const;
};

struct PassOptions {
  std::atomic<bool> *cancel = nullptr;
  TileCallback on_tile_finished;
  bool print_progress = false;
};

struct PassResult {
  double seconds = 0.0; // wall-clock time of the pass (rendering only)
  bool cancelled = false;
  uint64_t camera_rays = 0;
  uint64_t bounce_rays = 0;
  uint64_t shadow_rays = 0;
  std::vector<double> tile_ms;
  uint64_t total_rays() const { return camera_rays + bounce_rays + shadow_rays; }
};

color TraceRay(const ray &r, int depth, world &sceneWorld);

// Render a single sample for a specific pixel
color RenderPixel(world &sceneWorld, int x, int y, int sampleIndex);

// Number of path samples discarded for being NaN/Inf (should be 0).
long long InvalidSampleCount();

/**
 * @brief Add `spp` samples per pixel, with sample indices
 * [first_sample, first_sample + spp), to `buffers`.
 *
 * Every sample's random stream is a function of (pixel, sample index, seed)
 * only, so rendering 0..N in one pass or in several passes gives
 * bit-identical buffers. That is what makes progressive, equal-time
 * experiments reproducible.
 */
PassResult RenderPass(world &sceneWorld, RenderBuffers &buffers,
                      long long first_sample, int spp, unsigned int threads,
                      int tile_size, const PassOptions &options = {});

void RenderSceneToBitmap(world &sceneWorld, std::vector<color> &bitmap,
                         unsigned int threads, int tile_size, bool tile_debug,
                         const TileCallback &onTileFinished = TileCallback(),
                         std::atomic<bool> *cancelFlag = nullptr);

} // namespace render
