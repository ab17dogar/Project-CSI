#include "eval/progressive.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "engine/world.h"

namespace eval {

progressive_result render_progressive(world &scene,
                                      const progressive_settings &s) {
  if (s.time_budget <= 0.0 && s.max_spp <= 0)
    throw std::invalid_argument("progressive render needs a time budget or a "
                                "sample limit");
  scene.prepare(); // BVH and lights: not part of the timed render
  const int width = scene.GetImageWidth();
  const int height = scene.GetImageHeight();
  if (s.reference &&
      (s.reference->width != width || s.reference->height != height))
    throw std::invalid_argument("reference size does not match the image");

  progressive_result result;
  result.buffers.reset(width, height);
  double total = 0.0;
  double seconds_per_spp = 0.0;
  uint64_t cam = 0, bounce = 0, shadow = 0;

  for (int pass = 0;; ++pass) {
    const long long done = result.buffers.spp;
    long long n = std::max<long long>(1, done); // double the running total
    if (s.max_spp > 0)
      n = std::min(n, s.max_spp - done);
    if (s.time_budget > 0.0 && seconds_per_spp > 0.0) {
      const double remaining = s.time_budget - total;
      const long long fits =
          static_cast<long long>(std::floor(remaining / seconds_per_spp));
      n = std::min(n, fits);
    }
    if (n <= 0)
      break;
    n = std::min<long long>(n, 1 << 20);

    const render::PassResult pr =
        render::RenderPass(scene, result.buffers, done, static_cast<int>(n),
                           s.threads, s.tile_size);
    total += pr.seconds;
    seconds_per_spp = pr.seconds / static_cast<double>(n);
    cam += pr.camera_rays;
    bounce += pr.bounce_rays;
    shadow += pr.shadow_rays;

    pass_record rec;
    rec.pass = pass;
    rec.pass_spp = static_cast<int>(n);
    rec.spp = result.buffers.spp;
    rec.pass_seconds = pr.seconds;
    rec.total_seconds = total;
    rec.camera_rays = cam;
    rec.bounce_rays = bounce;
    rec.shadow_rays = shadow;
    if (s.reference) {
      const image_rgb img = result.buffers.mean();
      rec.error = metrics::compare(img, *s.reference, s.reference_variance,
                                   s.flip, s.flip_options);
      rec.has_metrics = true;
    }
    result.passes.push_back(rec);
    if (s.on_pass)
      s.on_pass(rec);

    if (s.time_budget > 0.0 && total >= s.time_budget)
      break;
    if (s.max_spp > 0 && result.buffers.spp >= s.max_spp)
      break;
  }
  return result;
}

std::string csv_header() {
  return "scene,method,pass,pass_spp,spp,pass_seconds,total_seconds,"
         "camera_rays,bounce_rays,shadow_rays,mrays_per_s,mse,rmse,relmse,"
         "mape,mse_corrected,relmse_corrected,flip";
}

std::string csv_row(const pass_record &p, const std::string &scene,
                    const std::string &method) {
  std::ostringstream os;
  os.precision(10);
  const double rays =
      static_cast<double>(p.camera_rays + p.bounce_rays + p.shadow_rays);
  os << scene << ',' << method << ',' << p.pass << ',' << p.pass_spp << ','
     << p.spp << ',' << p.pass_seconds << ',' << p.total_seconds << ','
     << p.camera_rays << ',' << p.bounce_rays << ',' << p.shadow_rays << ','
     << (p.total_seconds > 0.0 ? rays / p.total_seconds / 1e6 : 0.0);
  if (p.has_metrics) {
    const metrics::report &e = p.error;
    os << ',' << e.mse << ',' << e.rmse << ',' << e.relmse << ',' << e.mape;
    if (e.corrected)
      os << ',' << e.mse_corrected << ',' << e.relmse_corrected;
    else
      os << ",,";
    if (e.flip >= 0.0)
      os << ',' << e.flip;
    else
      os << ',';
  } else {
    os << ",,,,,,,";
  }
  return os.str();
}

} // namespace eval
