#include "engine/render_runner.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

#include "engine/camera.h"
#include "engine/config.h"
#include "engine/integrator.h"
#include "engine/oidn_denoiser.h"
#include "engine/world.h"
#include "util/logging.h"
#include "util/ray.h"

namespace render {
namespace {

struct Tile {
  int x0;
  int y0;
  int w;
  int h;
};

// Camera-space coordinate of a jittered sample inside pixel (x, y). Pixel i
// spans [i, i+1) of the image width (the old /(width - 1) stretched the
// image by one pixel).
inline ray CameraRay(const camera &cam, int x, int y, int width, int height) {
  const double u = (x + random_double()) / static_cast<double>(width);
  const double v = (y + random_double()) / static_cast<double>(height);
  return cam.get_ray(u, v);
}

// A path that produced NaN/Inf is a bug. It is dropped (it would otherwise
// poison the pixel) but counted, so the problem cannot hide.
std::atomic<long long> g_invalid_samples{0};

inline color Sanitize(const color &c) {
  if (is_finite(c))
    return c;
  g_invalid_samples.fetch_add(1, std::memory_order_relaxed);
  return color(0, 0, 0);
}

} // namespace

color TraceRay(const ray &r, int depth, world &sceneWorld) {
  sceneWorld.prepare();
  path_tracer_settings settings = settings_from_config(*sceneWorld.pconfig);
  settings.max_depth = depth;
  return Sanitize(trace_path(r, sceneWorld, settings));
}

color RenderPixel(world &sceneWorld, int x, int y, int sampleIndex) {
  (void)sampleIndex; // interactive mode keeps one running stream per thread
  sceneWorld.prepare();
  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  const ray r = CameraRay(*sceneWorld.pcamera, x, y, width, height);
  return Sanitize(
      trace_path(r, sceneWorld, settings_from_config(*sceneWorld.pconfig)));
}

long long InvalidSampleCount() { return g_invalid_samples.load(); }

void RenderSceneToBitmap(world &sceneWorld, std::vector<color> &bitmap,
                         unsigned int threads, int tile_size, bool tile_debug,
                         const TileCallback &onTileFinished,
                         std::atomic<bool> *cancelFlag) {
  // Builds the BVH (if selected) and the light list, once.
  sceneWorld.prepare();
  const path_tracer_settings settings =
      settings_from_config(*sceneWorld.pconfig);
  const uint64_t seed = sceneWorld.pconfig->seed;

  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  const int samples = sceneWorld.GetSamplesPerPixel();
  std::shared_ptr<camera> camera = sceneWorld.pcamera;

  if (tile_size <= 0) {
    tile_size = 16;
  }
  if (tile_size > width) {
    tile_size = width;
  }

  bitmap.clear();
  bitmap.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

  std::vector<Tile> tiles;
  for (int y = 0; y < height; y += tile_size) {
    for (int x = 0; x < width; x += tile_size) {
      const int w = std::min(tile_size, width - x);
      const int h = std::min(tile_size, height - y);
      tiles.push_back({x, y, w, h});
    }
  }

  struct TileStat {
    int x0;
    int y0;
    int w;
    int h;
    long long us;
  };

  std::vector<TileStat> tile_stats;
  std::mutex tile_stats_mtx;
  std::mutex print_mtx;
  std::atomic<size_t> next_tile{0};
  std::atomic<size_t> tiles_done{0};
  const size_t total_tiles = tiles.size();
  std::atomic<long long> total_tile_time_us{0};
  std::atomic<bool> cancelled{false};

  auto worker = [&](unsigned int thread_index) {
    (void)thread_index;
    while (true) {
      if (cancelFlag && cancelFlag->load()) {
        cancelled = true;
        break;
      }

      const size_t tileIndex = next_tile.fetch_add(1);
      if (tileIndex >= total_tiles) {
        break;
      }
      const Tile tile = tiles[tileIndex];

      const auto tile_t0 = std::chrono::high_resolution_clock::now();

      for (int yy = tile.y0; yy < tile.y0 + tile.h; ++yy) {
        if (cancelFlag && cancelFlag->load()) {
          cancelled = true;
          break;
        }
        for (int xx = tile.x0; xx < tile.x0 + tile.w; ++xx) {
          color pixel_color(0, 0, 0);
          const uint64_t pixel_index =
              static_cast<uint64_t>(yy) * static_cast<uint64_t>(width) +
              static_cast<uint64_t>(xx);
          for (int s = 0; s < samples; ++s) {
            // Each (pixel, sample) gets its own stream: the image does not
            // depend on thread count or tile order.
            seed_thread_rng(pixel_index, static_cast<uint64_t>(s), seed);
            const ray r = CameraRay(*camera, xx, yy, width, height);
            pixel_color += Sanitize(trace_path(r, sceneWorld, settings));
          }
          bitmap[(height - 1 - yy) * width + xx] = pixel_color;
        }
        if (cancelFlag && cancelFlag->load()) {
          break;
        }
      }

      if (cancelFlag && cancelFlag->load()) {
        cancelled = true;
        break;
      }

      const auto tile_t1 = std::chrono::high_resolution_clock::now();
      const long long us =
          std::chrono::duration_cast<std::chrono::microseconds>(tile_t1 -
                                                                tile_t0)
              .count();
      total_tile_time_us.fetch_add(us);
      {
        std::lock_guard<std::mutex> lock(tile_stats_mtx);
        tile_stats.push_back({tile.x0, tile.y0, tile.w, tile.h, us});
      }

      const size_t done = ++tiles_done;
      const double avg_us =
          done ? static_cast<double>(total_tile_time_us.load()) /
                     static_cast<double>(done)
               : 0.0;
      const size_t remaining = total_tiles > done ? total_tiles - done : 0;
      const double est_remaining_ms =
          (avg_us * static_cast<double>(remaining)) / 1000.0;

      if (!g_quiet.load()) {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cerr << "\rTiles remaining: " << remaining
                  << " | ETA: " << (est_remaining_ms / 1000.0) << " s"
                  << std::flush;
      }

      if (onTileFinished) {
        TileProgressStats stats;
        stats.tilesDone = done;
        stats.totalTiles = total_tiles;
        stats.avgTileMs = avg_us / 1000.0;
        stats.estRemainingMs = est_remaining_ms;
        onTileFinished(bitmap, width, height, stats);
      }
    }
  };

  const unsigned int hw_threads = std::thread::hardware_concurrency();
  const unsigned int max_threads = hw_threads ? hw_threads : threads;
  const unsigned int nthreads =
      std::max<unsigned int>(1, std::min<unsigned int>(threads, max_threads));

  std::vector<std::thread> pool;
  pool.reserve(nthreads);

  const auto tstart = std::chrono::high_resolution_clock::now();
  for (unsigned int i = 0; i < nthreads; ++i) {
    pool.emplace_back(worker, i);
  }
  for (auto &th : pool) {
    if (th.joinable()) {
      th.join();
    }
  }
  const auto tend = std::chrono::high_resolution_clock::now();
  const double total_ms =
      std::chrono::duration<double, std::milli>(tend - tstart).count();

  if (!g_quiet.load()) {
    std::lock_guard<std::mutex> lock(print_mtx);
    if (cancelled.load()) {
      std::cerr << "\rRender cancelled after " << total_ms << " ms\n";
    } else {
      std::cerr << "\rTiles remaining: 0\n";
      std::cerr << "Render time: " << total_ms << " ms\n";
      const long long bad = g_invalid_samples.load();
      if (bad > 0)
        std::cerr << "WARNING: " << bad
                  << " samples returned NaN/Inf and were discarded\n";
    }
  }

  // OIDN denoising pass (if enabled and not cancelled)
  if (!cancelled.load() && sceneWorld.pconfig &&
      sceneWorld.pconfig->enableDenoiser) {
    oidn_denoiser denoiser;
    if (oidn_denoiser::is_available()) {
      if (!g_quiet.load()) {
        std::cerr << "Applying OIDN denoiser (" << oidn_denoiser::version()
                  << ")...\n";
      }

      // Normalize samples (divide by sample count for averaging)
      const int samples = sceneWorld.GetSamplesPerPixel();
      std::vector<color> normalized_bitmap(bitmap.size());
      for (size_t i = 0; i < bitmap.size(); i++) {
        normalized_bitmap[i] = bitmap[i] / static_cast<double>(samples);
      }

      // Denoise in HDR mode
      auto denoised = denoiser.denoise(normalized_bitmap, width, height, true);

      // Scale back to accumulated format (multiply by samples so downstream
      // gamma correction works)
      for (size_t i = 0; i < bitmap.size(); i++) {
        bitmap[i] = denoised[i] * static_cast<double>(samples);
      }

      if (!g_quiet.load()) {
        std::cerr << "Denoising complete.\n";
      }
    }
  }

  if (!g_quiet.load() && (g_verbose.load() || tile_debug)) {
    std::lock_guard<std::mutex> lock(tile_stats_mtx);
    if (!tile_stats.empty()) {
      long long min_us = tile_stats[0].us;
      long long max_us = tile_stats[0].us;
      long long sum_us = 0;
      for (const auto &stat : tile_stats) {
        min_us = std::min(min_us, stat.us);
        max_us = std::max(max_us, stat.us);
        sum_us += stat.us;
      }

      const double avg_ms = static_cast<double>(sum_us) /
                            static_cast<double>(tile_stats.size()) / 1000.0;
      const double min_ms = static_cast<double>(min_us) / 1000.0;
      const double max_ms = static_cast<double>(max_us) / 1000.0;
      std::cerr << "Tile stats: count=" << tile_stats.size()
                << ", avg=" << avg_ms << " ms, min=" << min_ms
                << " ms, max=" << max_ms << " ms\n";

      if (tile_debug) {
        for (const auto &stat : tile_stats) {
          std::cerr << "  tile(" << stat.x0 << "," << stat.y0 << ") " << stat.w
                    << "x" << stat.h << ": "
                    << (static_cast<double>(stat.us) / 1000.0) << " ms\n";
        }
      }
    }
  }
}

} // namespace render
