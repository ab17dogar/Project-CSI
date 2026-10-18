#include "engine/render_runner.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
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

std::vector<Tile> MakeTiles(int width, int height, int tile_size) {
  if (tile_size <= 0)
    tile_size = 16;
  tile_size = std::min(tile_size, std::max(width, 1));
  std::vector<Tile> tiles;
  for (int y = 0; y < height; y += tile_size)
    for (int x = 0; x < width; x += tile_size)
      tiles.push_back({x, y, std::min(tile_size, width - x),
                       std::min(tile_size, height - y)});
  return tiles;
}

unsigned int ThreadCount(unsigned int requested) {
  const unsigned int hw = std::thread::hardware_concurrency();
  const unsigned int cap = hw ? hw : requested;
  return std::max<unsigned int>(1, std::min<unsigned int>(requested, cap));
}

} // namespace

// --------------------------------------------------------------------------
// Buffers
// --------------------------------------------------------------------------

void RenderBuffers::reset(int w, int h) {
  width = w;
  height = h;
  spp = 0;
  const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h);
  sum.assign(n, color(0, 0, 0));
  sum_sq.assign(n, color(0, 0, 0));
}

image_rgb RenderBuffers::mean() const {
  image_rgb img(width, height);
  const double inv = spp > 0 ? 1.0 / static_cast<double>(spp) : 0.0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
      img.set(x, y, sum[static_cast<size_t>(y) * width + x] * inv);
  return img;
}

image_rgb RenderBuffers::variance_of_mean() const {
  image_rgb img(width, height);
  if (spp < 2)
    return img;
  const double n = static_cast<double>(spp);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const size_t i = static_cast<size_t>(y) * width + x;
      color v;
      for (int k = 0; k < 3; ++k) {
        // Unbiased sample variance, then divided by n for the mean.
        const double s2 = (sum_sq[i][k] - sum[i][k] * sum[i][k] / n) / (n - 1);
        v[k] = std::max(0.0, s2) / n;
      }
      img.set(x, y, v);
    }
  }
  return img;
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------

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

PassResult RenderPass(world &sceneWorld, RenderBuffers &buffers,
                      long long first_sample, int spp, unsigned int threads,
                      int tile_size, const PassOptions &options) {
  // Builds the BVH (if selected) and the light list, once.
  sceneWorld.prepare();
  const path_tracer_settings settings =
      settings_from_config(*sceneWorld.pconfig);
  const uint64_t seed = sceneWorld.pconfig->seed;
  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  if (buffers.width != width || buffers.height != height)
    buffers.reset(width, height);
  const camera &cam = *sceneWorld.pcamera;

  const std::vector<Tile> tiles = MakeTiles(width, height, tile_size);
  const size_t total_tiles = tiles.size();

  PassResult result;
  std::vector<double> tile_ms(total_tiles, 0.0);
  std::mutex print_mtx;
  std::atomic<size_t> next_tile{0};
  std::atomic<size_t> tiles_done{0};
  std::atomic<long long> total_tile_us{0};
  std::atomic<bool> cancelled{false};
  std::atomic<uint64_t> camera_rays{0}, bounce_rays{0}, shadow_rays{0};
  std::atomic<bool> *const cancel_flag = options.cancel;
  auto should_cancel = [&]() { return cancel_flag && cancel_flag->load(); };

  auto worker = [&]() {
    const ray_counters before = thread_ray_counters();
    while (true) {
      if (should_cancel()) {
        cancelled = true;
        break;
      }
      const size_t tile_index = next_tile.fetch_add(1);
      if (tile_index >= total_tiles)
        break;
      const Tile tile = tiles[tile_index];
      const auto t0 = std::chrono::steady_clock::now();

      for (int yy = tile.y0; yy < tile.y0 + tile.h && !should_cancel(); ++yy) {
        for (int xx = tile.x0; xx < tile.x0 + tile.w; ++xx) {
          const uint64_t pixel_index =
              static_cast<uint64_t>(yy) * static_cast<uint64_t>(width) +
              static_cast<uint64_t>(xx);
          const size_t i = static_cast<size_t>(height - 1 - yy) * width + xx;
          color &s1 = buffers.sum[i];
          color &s2 = buffers.sum_sq[i];
          for (int s = 0; s < spp; ++s) {
            // Each (pixel, sample index) gets its own stream, so the image
            // depends neither on thread count or tile order nor on how the
            // samples are split into passes. Samples are added to the buffer
            // one by one, so the floating-point summation order is the same
            // too: a split render is bit-identical to a single pass.
            seed_thread_rng(pixel_index,
                            static_cast<uint64_t>(first_sample + s), seed);
            const ray r = CameraRay(cam, xx, yy, width, height);
            const color L = Sanitize(trace_path(r, sceneWorld, settings));
            s1 += L;
            s2 += L * L;
          }
        }
      }
      if (should_cancel()) {
        cancelled = true;
        break;
      }

      const long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::steady_clock::now() - t0)
                               .count();
      tile_ms[tile_index] = us / 1000.0;
      total_tile_us.fetch_add(us);
      const size_t done = ++tiles_done;
      const double avg_us = static_cast<double>(total_tile_us.load()) /
                            static_cast<double>(done);
      const size_t remaining = total_tiles - done;
      const double eta_ms = avg_us * static_cast<double>(remaining) / 1000.0;

      if (options.print_progress && !g_quiet.load()) {
        std::lock_guard<std::mutex> lock(print_mtx);
        std::cerr << "\rTiles remaining: " << remaining
                  << " | ETA: " << (eta_ms / 1000.0) << " s" << std::flush;
      }
      if (options.on_tile_finished) {
        TileProgressStats stats;
        stats.tilesDone = done;
        stats.totalTiles = total_tiles;
        stats.avgTileMs = avg_us / 1000.0;
        stats.estRemainingMs = eta_ms;
        std::lock_guard<std::mutex> lock(print_mtx);
        options.on_tile_finished(buffers.sum, width, height, stats);
      }
    }
    const ray_counters &after = thread_ray_counters();
    camera_rays += after.camera - before.camera;
    bounce_rays += after.bounce - before.bounce;
    shadow_rays += after.shadow - before.shadow;
  };

  const unsigned int nthreads = ThreadCount(threads);
  std::vector<std::thread> pool;
  pool.reserve(nthreads);
  const auto tstart = std::chrono::steady_clock::now();
  for (unsigned int i = 0; i < nthreads; ++i)
    pool.emplace_back(worker);
  for (auto &th : pool)
    th.join();
  result.seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - tstart)
          .count();

  result.cancelled = cancelled.load();
  result.camera_rays = camera_rays.load();
  result.bounce_rays = bounce_rays.load();
  result.shadow_rays = shadow_rays.load();
  if (!result.cancelled)
    buffers.spp += spp;
  result.tile_ms = std::move(tile_ms);
  return result;
}

void RenderSceneToBitmap(world &sceneWorld, std::vector<color> &bitmap,
                         unsigned int threads, int tile_size, bool tile_debug,
                         const TileCallback &onTileFinished,
                         std::atomic<bool> *cancelFlag) {
  sceneWorld.prepare();
  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  const int samples = sceneWorld.GetSamplesPerPixel();

  RenderBuffers buffers;
  buffers.reset(width, height);
  PassOptions options;
  options.cancel = cancelFlag;
  options.on_tile_finished = onTileFinished;
  options.print_progress = true;
  const PassResult pass =
      RenderPass(sceneWorld, buffers, 0, samples, threads, tile_size, options);
  bitmap = buffers.sum; // sum of samples; callers divide by spp

  if (!g_quiet.load()) {
    if (pass.cancelled) {
      std::cerr << "\rRender cancelled after " << pass.seconds * 1000.0
                << " ms\n";
    } else {
      std::cerr << "\rTiles remaining: 0\n";
      std::cerr << "Render time: " << pass.seconds * 1000.0 << " ms ("
                << pass.total_rays() / pass.seconds / 1e6 << " Mrays/s)\n";
      const long long bad = g_invalid_samples.load();
      if (bad > 0)
        std::cerr << "WARNING: " << bad
                  << " samples returned NaN/Inf and were discarded\n";
    }
  }

  // OIDN denoising pass (if enabled and not cancelled)
  if (!pass.cancelled && sceneWorld.pconfig &&
      sceneWorld.pconfig->enableDenoiser) {
    oidn_denoiser denoiser;
    if (oidn_denoiser::is_available()) {
      if (!g_quiet.load()) {
        std::cerr << "Applying OIDN denoiser (" << oidn_denoiser::version()
                  << ")...\n";
      }

      // Normalize samples (divide by sample count for averaging)
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

  if (!g_quiet.load() && (g_verbose.load() || tile_debug) &&
      !pass.tile_ms.empty()) {
    const auto [mn, mx] =
        std::minmax_element(pass.tile_ms.begin(), pass.tile_ms.end());
    double sum_ms = 0;
    for (double ms : pass.tile_ms)
      sum_ms += ms;
    std::cerr << "Tile stats: count=" << pass.tile_ms.size()
              << ", avg=" << sum_ms / pass.tile_ms.size() << " ms, min=" << *mn
              << " ms, max=" << *mx << " ms\n";
    if (tile_debug) {
      const std::vector<Tile> tiles = MakeTiles(width, height, tile_size);
      for (size_t i = 0; i < tiles.size(); ++i)
        std::cerr << "  tile(" << tiles[i].x0 << "," << tiles[i].y0 << ") "
                  << tiles[i].w << "x" << tiles[i].h << ": " << pass.tile_ms[i]
                  << " ms\n";
    }
  }
}

GeometryBuffers RenderGeometry(world &sceneWorld) {
  sceneWorld.prepare();
  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  const camera &cam = *sceneWorld.pcamera;
  const vec3 forward = unit_vector(cam.LOOK_AT - cam.LOOK_FROM);
  GeometryBuffers out{image_rgb(width, height), image_rgb(width, height)};
  for (int y = 0; y < height; ++y) {
    // Camera y runs upward; image rows run downward (see RenderPass).
    const int yy = height - 1 - y;
    for (int x = 0; x < width; ++x) {
      const ray r = cam.get_ray((x + 0.5) / width, (yy + 0.5) / height);
      hit_record rec;
      if (!sceneWorld.hit(r, 1e-9, INF, rec))
        continue;
      const double z = dot(rec.p - r.origin(), forward);
      out.depth.set(x, y, color(z, z, z));
      out.normal.set(x, y, rec.normal);
    }
  }
  return out;
}

} // namespace render
