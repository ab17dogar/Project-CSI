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
#include "engine/hdri_environment.h"
#include "engine/material.h"
#include "engine/mis.h"
#include "engine/oidn_denoiser.h"
#include "engine/point_light.h"
#include "engine/sun.h"
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

color TraceRayInternal(const ray &r, int depth, world &sceneWorld) {
  hit_record rec;

  if (depth <= 0) {
    return color(0, 0, 0);
  }

  if (sceneWorld.hit(r, 0.001, INF, rec)) {
    ray scattered;
    color attenuation;
    color result;

    // Get emission from material (non-zero for emissive materials)
    color emitted = rec.mat_ptr->emitted(rec.u, rec.v, rec.p);

    if (rec.mat_ptr->scatter(r, rec, attenuation, scattered)) {
      // Russian Roulette path termination after first few bounces
      // Probabilistically terminate paths with low contribution while
      // maintaining unbiased results
      int max_depth = sceneWorld.GetMaxDepth();
      if (depth < max_depth - 3) { // Only apply after first 3 bounces
        double luminance = 0.2126 * attenuation.x() + 0.7152 * attenuation.y() +
                           0.0722 * attenuation.z();
        double continue_prob = std::min(0.95, std::max(0.1, luminance));
        if (random_double() > continue_prob) {
          return emitted; // Terminate path, return emission only
        }
        // Adjust for probability of not terminating
        attenuation = attenuation / continue_prob;
      }

      result = attenuation * TraceRayInternal(scattered, depth - 1, sceneWorld);

      // Check if the scattered ray is refracted (going through glass)
      // Refracted rays go opposite to surface normal
      bool isRefracted = dot(scattered.direction(), rec.normal) < 0;

      if (!isRefracted) {
        // Apply sun lighting with shadow testing
        ray shadowRay;
        shadowRay.dir = sceneWorld.psun->direction;
        // Offset origin along normal to prevent shadow acne
        shadowRay.orig = rec.p + rec.normal * 0.001;
        hit_record shadowRec;
        if (sceneWorld.hit(shadowRay, 0.001, INF, shadowRec)) {
          result = result * 0.3; // Softer shadow
        } else {
          result = result * sceneWorld.psun->sunColor;
        }

        // Direct sampling of point lights (Next Event Estimation with MIS)
        for (const auto &light : sceneWorld.pointLights) {
          vec3 toLight = light->position - rec.p;
          double lightDist = toLight.length();
          vec3 lightDir = toLight / lightDist;

          // Check if light is visible (shadow ray)
          ray lightShadowRay;
          // Offset origin along normal to prevent shadow acne
          lightShadowRay.orig = rec.p + rec.normal * 0.001;
          lightShadowRay.dir = lightDir;
          hit_record lightShadowRec;

          if (!sceneWorld.hit(lightShadowRay, 0.001, lightDist - 0.001,
                              lightShadowRec)) {
            // Light is visible - calculate contribution with MIS
            double cosTheta = std::max(0.0, dot(rec.normal, lightDir));

            // PDF for light sampling (point light = delta function
            // approximation) For proper MIS, we use 1/distance^2 as the
            // effective PDF
            double pdf_light = 1.0;

            // PDF for BRDF sampling this direction (cosine-weighted for
            // diffuse)
            double pdf_brdf = mis::pdf_cosine_hemisphere(lightDir, rec.normal);

            // MIS weight using power heuristic
            double mis_weight = mis::power_heuristic(pdf_light, pdf_brdf);

            // Light attenuation (inverse square law)
            double light_attenuation =
                light->intensity / (lightDist * lightDist);

            // Apply MIS-weighted contribution
            result = result + light->lightColor * cosTheta * light_attenuation *
                                  mis_weight;
          }
        }
      }
      // Glass (refractive) materials - light passes through, no shadow
      // darkening
    } else {
      // Scatter returned false - use emission only
      result = color(0, 0, 0);
    }

    // Add emission to result
    return emitted + result;
  }

  // Sky background gradient or HDRI environment
  vec3 unit_direction = unit_vector(r.direction());

  // Use HDRI environment if available
  if (sceneWorld.hdri && sceneWorld.hdri->is_valid()) {
    return sceneWorld.hdri->sample(unit_direction);
  }

  // Check if sun is disabled (zero color means sun is off)
  color sunCol = sceneWorld.psun->sunColor;
  double sunBrightness = sunCol.x() + sunCol.y() + sunCol.z();

  if (sunBrightness < 0.001) {
    // Sun is disabled - return black sky
    return color(0.0, 0.0, 0.0);
  }

  // Separate sky and ground based on ray direction
  double y = unit_direction.y();

  color result;
  if (y > 0.0) {
    // SKY: Above horizon - blend from horizon to zenith
    double t = y; // 0 at horizon, 1 at zenith
    result = (1.0 - t) * sceneWorld.skyColorBottom + t * sceneWorld.skyColorTop;
  } else {
    // GROUND: Below horizon - use ground color with slight fade
    double t =
        std::min(1.0, -y * 2.0); // 0 at horizon, 1 when looking straight down
    result = (1.0 - t) * sceneWorld.skyColorBottom + t * sceneWorld.groundColor;
  }

  // Scale by sun intensity (normalized)
  double normalizedBrightness = std::min(1.0, sunBrightness / 3.0);
  return result * normalizedBrightness;
}

} // namespace

color TraceRay(const ray &r, int depth, world &sceneWorld) {
  return TraceRayInternal(r, depth, sceneWorld);
}

color RenderPixel(world &sceneWorld, int x, int y, int sampleIndex) {
  const int width = sceneWorld.GetImageWidth();
  const int height = sceneWorld.GetImageHeight();
  std::shared_ptr<camera> cam = sceneWorld.pcamera;

  // Use sample index for deterministic jittering
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> dist(0.0, 1.0);

  const double u = (x + dist(gen)) / static_cast<double>(width - 1);
  const double v = (y + dist(gen)) / static_cast<double>(height - 1);
  const ray r = cam->get_ray(u, v);

  return TraceRayInternal(r, sceneWorld.GetMaxDepth(), sceneWorld);
}

void RenderSceneToBitmap(world &sceneWorld, std::vector<color> &bitmap,
                         unsigned int threads, int tile_size, bool tile_debug,
                         const TileCallback &onTileFinished,
                         std::atomic<bool> *cancelFlag) {
  // Build BVH if BVH acceleration is selected and not already built
  if (sceneWorld.GetAccelerationMethod() == AccelerationMethod::BVH &&
      !sceneWorld.hasBVH()) {
    sceneWorld.buildBVH();
  }

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
    std::mt19937 gen(static_cast<unsigned int>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()) ^
        static_cast<unsigned int>(std::chrono::high_resolution_clock::now()
                                      .time_since_epoch()
                                      .count())));
    std::uniform_real_distribution<double> dist(0.0, 1.0);

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
          for (int s = 0; s < samples; ++s) {
            const double u = (xx + dist(gen)) / static_cast<double>(width - 1);
            const double v = (yy + dist(gen)) / static_cast<double>(height - 1);
            const ray r = camera->get_ray(u, v);
            pixel_color +=
                TraceRayInternal(r, sceneWorld.GetMaxDepth(), sceneWorld);
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
