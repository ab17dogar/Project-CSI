// rt_compare: error metrics of a render against a reference (both .pfm).
//
//   rt_compare test.pfm reference.pfm [--ref-variance var.pfm] [--no-flip]
//              [--ldr] [--ppd N] [--flip-map out.png] [--eps E] [--json]
//
// Prints a human-readable summary, or one JSON object with --json.
// Exit codes: 0 ok, 1 usage error, 2 I/O error, 3 the test image has
// non-finite pixels.
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

#include "eval/metrics.h"
#include "util/image_io.h"

namespace {

int usage() {
  std::cerr << "usage: rt_compare test.pfm reference.pfm [--ref-variance var.pfm]\n"
               "                  [--no-flip] [--ldr] [--ppd N] [--flip-map out.png]\n"
               "                  [--eps E] [--json]\n";
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  std::string test_path, ref_path, var_path, flip_map_path;
  bool with_flip = true, json = false;
  metrics::flip_options fo;
  double eps = 1e-2;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--ref-variance" && i + 1 < argc)
      var_path = argv[++i];
    else if (a == "--no-flip")
      with_flip = false;
    else if (a == "--ldr")
      fo.hdr = false;
    else if (a == "--ppd" && i + 1 < argc)
      fo.ppd = std::atof(argv[++i]);
    else if (a == "--flip-map" && i + 1 < argc)
      flip_map_path = argv[++i];
    else if (a == "--eps" && i + 1 < argc)
      eps = std::atof(argv[++i]);
    else if (a == "--json")
      json = true;
    else if (a == "-h" || a == "--help")
      return usage();
    else if (!a.empty() && a[0] == '-')
      return usage();
    else if (test_path.empty())
      test_path = a;
    else if (ref_path.empty())
      ref_path = a;
    else
      return usage();
  }
  if (test_path.empty() || ref_path.empty())
    return usage();

  image_rgb test, ref, var;
  std::string err;
  if (!read_pfm(test_path, test, &err) || !read_pfm(ref_path, ref, &err) ||
      (!var_path.empty() && !read_pfm(var_path, var, &err))) {
    std::cerr << "rt_compare: " << err << "\n";
    return 2;
  }
  if (test.width != ref.width || test.height != ref.height) {
    std::cerr << "rt_compare: size mismatch (" << test.width << "x"
              << test.height << " vs " << ref.width << "x" << ref.height
              << ")\n";
    return 2;
  }

  metrics::report r =
      metrics::compare(test, ref, var_path.empty() ? nullptr : &var,
                       /*with_flip=*/false, fo, eps);
  if (with_flip || !flip_map_path.empty()) {
    image_rgb heat;
    r.flip = metrics::flip(test, ref, fo,
                           flip_map_path.empty() ? nullptr : &heat);
    if (!flip_map_path.empty() && !write_png_display(flip_map_path, heat)) {
      std::cerr << "rt_compare: cannot write " << flip_map_path << "\n";
      return 2;
    }
  }

  if (json) {
    std::cout << metrics::to_json(r) << "\n";
  } else {
    std::printf("pixels      %zu (%zu non-finite)\n", r.pixels, r.invalid_pixels);
    std::printf("MSE         %.6e\n", r.mse);
    std::printf("RMSE        %.6e\n", r.rmse);
    std::printf("relMSE      %.6e\n", r.relmse);
    std::printf("MAPE        %.6e\n", r.mape);
    if (r.corrected) {
      std::printf("MSE  (ref-noise corrected)   %.6e\n", r.mse_corrected);
      std::printf("relMSE (ref-noise corrected) %.6e\n", r.relmse_corrected);
    }
    if (r.flip >= 0.0)
      std::printf("%s  %.6f\n", fo.hdr ? "HDR-FLIP" : "LDR-FLIP", r.flip);
  }
  return r.invalid_pixels > 0 ? 3 : 0;
}
