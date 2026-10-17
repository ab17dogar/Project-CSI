// Measurement infrastructure: image I/O, error metrics, variance estimates,
// progressive rendering, and a statistical regression test against stored
// references.
#include "test_framework.h"
#include "test_helpers.h"

#include "engine/camera.h"
#include "engine/emissive.h"
#include "engine/factories/factory_methods.h"
#include "engine/lambertian.h"
#include "engine/pbr_material.h"
#include "engine/quad.h"
#include "engine/render_runner.h"
#include "engine/sphere.h"
#include "eval/metrics.h"
#include "eval/progressive.h"
#include "util/image_io.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#ifndef RAYTRACER_SOURCE_DIR
#define RAYTRACER_SOURCE_DIR "."
#endif

namespace {

namespace fs = std::filesystem;

fs::path temp_dir() {
  const fs::path d = fs::temp_directory_path() / "raytracer_tests_eval";
  fs::create_directories(d);
  return d;
}

image_rgb random_image(int w, int h, uint64_t seed, double scale = 1.0) {
  image_rgb img(w, h);
  seed_thread_rng(seed, 0);
  for (float &v : img.data)
    v = static_cast<float>(scale * random_double());
  return img;
}

// Small open box with a ceiling light: cheap, every pixel well-behaved.
std::shared_ptr<world> tiny_scene(int w, int h, int depth = 4) {
  auto wd = th::make_world(depth);
  wd->pconfig->IMAGE_WIDTH = w;
  wd->pconfig->IMAGE_HEIGHT = h;
  wd->pcamera = std::make_shared<camera>(point3(0, 1, 3), point3(0, 0.8, 0),
                                         vec3(0, 1, 0), 50.0, double(w) / h);
  auto white = std::make_shared<lambertian>(color(0.7, 0.7, 0.7));
  wd->objects.push_back(std::make_shared<quad>(point3(-2, 0, -2), vec3(0, 0, 4), vec3(4, 0, 0), white));
  wd->objects.push_back(std::make_shared<quad>(point3(-2, 0, -2), vec3(4, 0, 0), vec3(0, 3, 0), white));
  wd->objects.push_back(std::make_shared<sphere>(
      point3(0.3, 0.5, -0.4), 0.5, std::make_shared<pbr_material>(color(0.8, 0.4, 0.2), 0.0f, 0.3f)));
  wd->objects.push_back(std::make_shared<quad>(point3(-0.5, 2.5, -0.5), vec3(1, 0, 0), vec3(0, 0, 1),
                                               std::make_shared<emissive>(color(6, 6, 6))));
  return wd;
}

bool buffers_identical(const render::RenderBuffers &a, const render::RenderBuffers &b) {
  if (a.spp != b.spp || a.sum.size() != b.sum.size())
    return false;
  return std::memcmp(a.sum.data(), b.sum.data(), a.sum.size() * sizeof(color)) == 0 &&
         std::memcmp(a.sum_sq.data(), b.sum_sq.data(), a.sum_sq.size() * sizeof(color)) == 0;
}

} // namespace

TEST(pfm_roundtrip_and_variants) {
  const fs::path dir = temp_dir();
  const image_rgb img = random_image(17, 9, 3, 100.0);
  const fs::path p = dir / "roundtrip.pfm";
  CHECK(write_pfm(p.string(), img), "write PFM");
  image_rgb back;
  std::string err;
  CHECK(read_pfm(p.string(), back, &err), "read PFM: " << err);
  CHECK(back.width == 17 && back.height == 9, "dimensions survive");
  CHECK(back.data == img.data, "pixels survive bit-exactly");

  // Hand-written big-endian grayscale file: 2x1, values 1.5 and -2.
  const fs::path g = dir / "gray_be.pfm";
  {
    std::ofstream out(g, std::ios::binary);
    out << "Pf\n2 1\n1.0\n";
    const unsigned char be[8] = {0x3f, 0xc0, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x00};
    out.write(reinterpret_cast<const char *>(be), 8);
  }
  image_rgb gray;
  CHECK(read_pfm(g.string(), gray, &err), "read big-endian grayscale: " << err);
  CHECK(gray.width == 2 && gray.at(0, 0).x() == 1.5 && gray.at(0, 0).z() == 1.5 &&
            gray.at(1, 0).y() == -2.0,
        "grayscale replicated to RGB, byte order honored");

  CHECK(!read_pfm((dir / "missing.pfm").string(), gray, &err), "missing file fails");
  std::ofstream(dir / "bad.pfm") << "P6\n1 1\n255\nxxx";
  CHECK(!read_pfm((dir / "bad.pfm").string(), gray, &err), "non-PFM fails");
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST(error_metrics_match_definitions) {
  const image_rgb ref = random_image(32, 16, 5, 2.0);
  metrics::report same = metrics::compare(ref, ref, nullptr, true);
  CHECK(same.mse == 0 && same.relmse == 0 && same.mape == 0, "identical images: zero error");
  CHECK_NEAR(same.flip, 0.0, 1e-6, "identical images: zero FLIP");

  // Constant offset d: MSE = d^2, relMSE = mean d^2 / (r^2 + eps).
  const double d = 0.125;
  image_rgb test = ref;
  double rel = 0, ape = 0;
  for (size_t i = 0; i < test.data.size(); ++i) {
    test.data[i] = static_cast<float>(ref.data[i] + d);
    const double r = ref.data[i];
    rel += (static_cast<double>(test.data[i]) - r) * (static_cast<double>(test.data[i]) - r) /
           (r * r + 1e-2);
    ape += std::fabs(static_cast<double>(test.data[i]) - r) / (r + 1e-2);
  }
  metrics::report off = metrics::compare(test, ref);
  CHECK_NEAR(off.mse, d * d, 1e-9, "MSE of a constant offset");
  CHECK_NEAR(off.rmse, d, 1e-9, "RMSE");
  CHECK_NEAR(off.relmse, rel / test.data.size(), 1e-9, "relMSE definition");
  CHECK_NEAR(off.mape, ape / test.data.size(), 1e-9, "MAPE definition");

  // Reference-noise correction subtracts the mean reference variance.
  image_rgb var(32, 16, 0.004f);
  metrics::report corr = metrics::compare(test, ref, &var);
  CHECK(corr.corrected, "correction applied");
  CHECK_NEAR(corr.mse_corrected, d * d - 0.004, 1e-9, "MSE correction");

  // Non-finite test pixels are excluded and counted.
  test.data[0] = std::numeric_limits<float>::quiet_NaN();
  metrics::report nan = metrics::compare(test, ref);
  CHECK(nan.invalid_pixels == 1 && std::isfinite(nan.mse), "NaN pixel excluded and counted");

  bool threw = false;
  try {
    metrics::compare(image_rgb(4, 4), image_rgb(5, 4));
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw, "size mismatch throws");
}

TEST(flip_orders_images_by_visible_error) {
  // Smooth HDR gradient reference; more noise must never score better.
  image_rgb ref(64, 64);
  for (int y = 0; y < 64; ++y)
    for (int x = 0; x < 64; ++x)
      ref.set(x, y, color(0.1 + x / 32.0, 0.2 + y / 40.0, 0.5));
  double prev = 0.0;
  for (double sigma : {0.01, 0.05, 0.2, 0.8}) {
    image_rgb noisy = ref;
    seed_thread_rng(9, 9);
    for (float &v : noisy.data)
      v = static_cast<float>(std::max(0.0, v * (1.0 + sigma * (2 * random_double() - 1))));
    const double f = metrics::flip(noisy, ref);
    CHECK(f > prev && f < 1.0, "FLIP increases with noise (sigma " << sigma << ": " << f << ")");
    prev = f;
  }
}

// The per-pixel variance the renderer reports must match the actual spread
// of pixel means across independent renders; reference-noise correction
// relies on it.
TEST(reported_variance_matches_empirical_variance) {
  auto wd = tiny_scene(6, 4);
  const int spp = 16, seeds = 300;
  const size_t npx = 6 * 4;
  std::vector<double> mean_sum(npx * 3, 0), mean_sq(npx * 3, 0), predicted(npx * 3, 0);
  for (int s = 0; s < seeds; ++s) {
    wd->pconfig->seed = 100 + s;
    render::RenderBuffers b;
    render::RenderPass(*wd, b, 0, spp, 1, 8);
    const image_rgb m = b.mean(), v = b.variance_of_mean();
    for (size_t i = 0; i < m.data.size(); ++i) {
      mean_sum[i] += m.data[i];
      mean_sq[i] += static_cast<double>(m.data[i]) * m.data[i];
      predicted[i] += v.data[i];
    }
  }
  double emp_total = 0, pred_total = 0;
  for (size_t i = 0; i < npx * 3; ++i) {
    const double mu = mean_sum[i] / seeds;
    emp_total += (mean_sq[i] / seeds - mu * mu) * seeds / (seeds - 1);
    pred_total += predicted[i] / seeds;
  }
  CHECK(pred_total > 0, "nonzero variance");
  CHECK_NEAR(pred_total / emp_total, 1.0, 0.15,
             "predicted / empirical variance of the pixel mean");
}

TEST(split_passes_are_bit_identical_to_one_pass) {
  auto wd = tiny_scene(16, 12);
  wd->pconfig->seed = 77;
  render::RenderBuffers one, split;
  render::RenderPass(*wd, one, 0, 9, 3, 5);
  render::RenderPass(*wd, split, 0, 2, 1, 16);
  render::RenderPass(*wd, split, 2, 4, 4, 3);
  render::RenderPass(*wd, split, 6, 3, 2, 7);
  CHECK(buffers_identical(one, split), "9 spp in one pass == 2 + 4 + 3 spp");

  render::RenderBuffers other;
  render::RenderPass(*wd, other, 0, 9, 3, 5);
  CHECK(buffers_identical(one, other), "rerun is identical");
  wd->pconfig->seed = 78;
  render::RenderBuffers reseeded;
  render::RenderPass(*wd, reseeded, 0, 9, 3, 5);
  CHECK(!buffers_identical(one, reseeded), "a different seed changes the image");
}

TEST(progressive_schedule_respects_limits) {
  auto wd = tiny_scene(24, 16);
  {
    eval::progressive_settings ps;
    ps.max_spp = 37;
    ps.threads = 2;
    const eval::progressive_result r = eval::render_progressive(*wd, ps);
    CHECK(r.buffers.spp == 37, "stops exactly at max_spp");
    long long expect = 0;
    for (const auto &p : r.passes) {
      expect += p.pass_spp;
      CHECK(p.spp == expect, "cumulative spp is consistent");
    }
    CHECK(r.passes[0].pass_spp == 1 && r.passes[1].pass_spp == 1 && r.passes[2].pass_spp == 2,
          "passes double the running total");
    render::RenderBuffers single;
    render::RenderPass(*wd, single, 0, 37, 2, 32);
    CHECK(buffers_identical(r.buffers, single), "progressive == single pass at equal spp");
  }
  {
    eval::progressive_settings ps;
    ps.time_budget = 0.25;
    ps.threads = 2;
    const eval::progressive_result r = eval::render_progressive(*wd, ps);
    const double t = r.passes.back().total_seconds;
    const double last_pass = r.passes.back().pass_seconds;
    CHECK(t <= 0.25 + std::max(0.05, 2.0 * last_pass), "time budget respected (" << t << " s)");
    CHECK(t >= 0.25 * 0.8 || r.passes.size() > 30, "time budget mostly used (" << t << " s)");
  }
  bool threw = false;
  try {
    eval::render_progressive(*wd, {});
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  CHECK(threw, "no limit is rejected");
}

// Statistical regression test: render a benchmark scene at low resolution and
// compare against a stored high-spp reference in units of standard error.
// Any change that shifts the expected image (a biased estimator, an
// unintended material change) fails; noise alone does not.
//
// Statistics, chosen after measuring their null distributions over 20 seeds:
//  - Variances come from the reference (16384 spp) rescaled to the test's
//    sample count. Under the null hypothesis both sample the same
//    distribution, and the test's own estimate is unreliable for pixels that
//    see tiny bright lights a handful of times.
//  - z is per color channel: R, G and B of one pixel come from the same
//    paths and are strongly correlated, so they cannot be pooled as
//    independent.
//  - Broad bias: the median of z^2 over 4x4-pixel blocks (chi-square(1)
//    median 0.455). A mean would let one firefly block fail the test.
//  - Localized bias (e.g. only glossy highlights change): the fraction of
//    blocks with |z| > 3.29 (nominally 0.1%; heavy-tailed noise gives ~1%).
//  - Global bias: per-channel z of the image sum.
// Over 20 seeds at 2048 spp the worst values were: median z^2 0.67, tail
// fraction 1.04%, |global z| 2.29. The thresholds below leave a margin.
// Deliberate bugs it catches: emitters 1% brighter (global z ~6-7), a halved
// pdf in the MIS weight (18% of blocks beyond 3.29). GGX alpha +10% fails only
// narrowly (3.1% vs 3%): subtle highlight changes are the job of the BSDF
// unit tests, not of this scene-level check.
// Regenerate the references with tests/data/make_regression_refs.sh when the
// expected image changes on purpose.
TEST(statistical_regression_against_stored_references) {
  const fs::path root = RAYTRACER_SOURCE_DIR;
  const int test_spp = 2048;
  for (const char *scene : {"cornell_box", "veach_mis"}) {
    const fs::path base = root / "tests" / "data" / (std::string("regression_") + scene);
    image_rgb ref, ref_var;
    std::string err;
    const bool loaded = read_pfm(base.string() + ".pfm", ref, &err) &&
                        read_pfm(base.string() + "_var.pfm", ref_var, &err);
    CHECK(loaded, "load stored reference: " << err);
    std::ifstream meta(base.string() + ".txt");
    std::string key;
    double ref_spp = 0;
    meta >> key >> ref_spp;
    CHECK(key == "spp" && ref_spp > 0, scene << ": reference metadata (spp)");
    if (!loaded || !(ref_spp > 0))
      continue;

    auto wd = LoadScene((root / "benchmarks" / "scenes" / (std::string(scene) + ".xml")).string());
    CHECK(wd != nullptr, "load scene " << scene);
    if (!wd)
      continue;
    wd->pconfig->IMAGE_WIDTH = ref.width;
    wd->pconfig->IMAGE_HEIGHT = ref.height;
    wd->pconfig->seed = 7; // independent of the reference's seed
    wd->pconfig->enableDenoiser = false;
    render::RenderBuffers b;
    render::RenderPass(*wd, b, 0, test_spp, 4, 16);
    const image_rgb test = b.mean();
    // Var(test - ref) per pixel: reference variance of the mean, plus the
    // same per-sample variance at the test's sample count.
    const double scale = 1.0 + ref_spp / test_spp;

    const int B = 4;
    std::vector<double> z2;
    for (int by = 0; by + B <= ref.height; by += B)
      for (int bx = 0; bx + B <= ref.width; bx += B)
        for (int c = 0; c < 3; ++c) {
          double d = 0, v = 0;
          for (int y = by; y < by + B; ++y)
            for (int x = bx; x < bx + B; ++x) {
              d += test.at(x, y)[c] - ref.at(x, y)[c];
              v += ref_var.at(x, y)[c] * scale;
            }
          if (v > 0)
            z2.push_back(d * d / v);
        }
    size_t tail = 0;
    for (double q : z2)
      tail += q > 10.83 ? 1 : 0; // |z| > 3.29
    const double tail_fraction = static_cast<double>(tail) / z2.size();
    std::nth_element(z2.begin(), z2.begin() + z2.size() / 2, z2.end());
    const double median_z2 = z2[z2.size() / 2];
    CHECK(median_z2 < 0.9, scene << ": median block z^2 = " << median_z2
                                 << " (expected ~0.455) over " << z2.size() << " blocks");
    CHECK(tail_fraction < 0.03, scene << ": " << 100 * tail_fraction
                                      << "% of blocks beyond |z| = 3.29");
    for (int c = 0; c < 3; ++c) {
      double d = 0, v = 0;
      for (int y = 0; y < ref.height; ++y)
        for (int x = 0; x < ref.width; ++x) {
          d += test.at(x, y)[c] - ref.at(x, y)[c];
          v += ref_var.at(x, y)[c] * scale;
        }
      const double z = d / std::sqrt(v);
      CHECK(std::fabs(z) < 4.0, scene << ": global bias z = " << z << " in channel " << c);
    }
  }
}
