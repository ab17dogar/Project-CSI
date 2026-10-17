// Checks that metrics::flip reproduces the reference values published with
// NVIDIA's FLIP (src/tests/test.py in https://github.com/NVlabs/flip):
//   LDR-FLIP(reference.png, test.png) = 0.159693
//   HDR-FLIP(reference.exr, test.exr) = 0.283547
// Built and run by tools/validate_flip.sh, which fetches the FLIP repository
// for its test images and image loaders.
#include <cmath>
#include <cstdio>
#include <string>

#include "FLIP.h"         // from the FLIP repository, as in its own tool
#include "imagehelpers.h" // stb_image + tinyexr loaders from the FLIP repository

#include "eval/metrics.h"
#include "util/image_io.h"

static bool load(const std::string &path, bool srgb, image_rgb &out) {
  int w = 0, h = 0;
  float *px = nullptr;
  if (!ImageHelpers::loadImage(path, w, h, px))
    return false;
  out = image_rgb(w, h);
  for (size_t i = 0; i < out.data.size(); ++i)
    out.data[i] = srgb ? FLIP::color3::sRGBToLinearRGB(px[i]) : px[i];
  delete[] px;
  return true;
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: validate_flip <flip-repo>/images\n");
    return 2;
  }
  const std::string dir = argv[1];
  image_rgb ref_ldr, test_ldr, ref_hdr, test_hdr;
  if (!load(dir + "/reference.png", true, ref_ldr) ||
      !load(dir + "/test.png", true, test_ldr) ||
      !load(dir + "/reference.exr", false, ref_hdr) ||
      !load(dir + "/test.exr", false, test_hdr)) {
    std::fprintf(stderr, "failed to load FLIP test images from %s\n", dir.c_str());
    return 2;
  }
  metrics::flip_options ldr;
  ldr.hdr = false;
  const double ldr_mean = metrics::flip(test_ldr, ref_ldr, ldr);
  const double hdr_mean = metrics::flip(test_hdr, ref_hdr, {});
  const double expected_ldr = 0.159693, expected_hdr = 0.283547;
  // The FLIP tool reports means rounded to 6 decimals.
  const bool ok_ldr = std::fabs(ldr_mean - expected_ldr) < 5e-7;
  const bool ok_hdr = std::fabs(hdr_mean - expected_hdr) < 5e-7;
  std::printf("LDR-FLIP %.6f (expected %.6f) %s\n", ldr_mean, expected_ldr,
              ok_ldr ? "OK" : "MISMATCH");
  std::printf("HDR-FLIP %.6f (expected %.6f) %s\n", hdr_mean, expected_hdr,
              ok_hdr ? "OK" : "MISMATCH");
  return ok_ldr && ok_hdr ? 0 : 1;
}
