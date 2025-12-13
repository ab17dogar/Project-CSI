#include "oidn_denoiser.h"
#include <algorithm>
#include <cmath>

#ifdef USE_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

std::vector<color> oidn_denoiser::denoise(const std::vector<color> &input,
                                          int width, int height,
                                          bool hdr) const {
#ifdef USE_OIDN
  // Convert input to float array for OIDN
  std::vector<float> color_buffer(width * height * 3);
  for (int i = 0; i < width * height; i++) {
    color_buffer[i * 3 + 0] = static_cast<float>(input[i].x());
    color_buffer[i * 3 + 1] = static_cast<float>(input[i].y());
    color_buffer[i * 3 + 2] = static_cast<float>(input[i].z());
  }

  // Create output buffer
  std::vector<float> output_buffer(width * height * 3);

  // Create OIDN device
  oidn::DeviceRef device = oidn::newDevice();
  device.commit();

  // Create ray tracing filter
  oidn::FilterRef filter = device.newFilter("RT");
  filter.setImage("color", color_buffer.data(), oidn::Format::Float3, width,
                  height);
  filter.setImage("output", output_buffer.data(), oidn::Format::Float3, width,
                  height);
  filter.set("hdr", hdr);
  filter.commit();

  // Execute denoising
  filter.execute();

  // Check for errors
  const char *errorMessage;
  if (device.getError(errorMessage) != oidn::Error::None) {
    std::cerr << "OIDN Error: " << errorMessage << std::endl;
    return input; // Return original on error
  }

  // Convert back to color array
  std::vector<color> output(width * height);
  for (int i = 0; i < width * height; i++) {
    output[i] = color(output_buffer[i * 3 + 0], output_buffer[i * 3 + 1],
                      output_buffer[i * 3 + 2]);
  }

  std::cerr << "OIDN denoising complete (" << width << "x" << height << ")"
            << std::endl;
  return output;

#else
  // Fallback: simple bilateral filter when OIDN not available
  std::cerr << "OIDN not available, using bilateral filter fallback"
            << std::endl;

  std::vector<color> output(width * height);
  int kernel_size = 5;
  int half_kernel = kernel_size / 2;
  double sigma_spatial = 2.0;
  double sigma_range = 0.1;

  double spatial_coeff = -0.5 / (sigma_spatial * sigma_spatial);
  double range_coeff = -0.5 / (sigma_range * sigma_range);

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;
      color center = input[idx];

      color sum(0, 0, 0);
      double weight_sum = 0.0;

      for (int ky = -half_kernel; ky <= half_kernel; ky++) {
        for (int kx = -half_kernel; kx <= half_kernel; kx++) {
          int nx = std::clamp(x + kx, 0, width - 1);
          int ny = std::clamp(y + ky, 0, height - 1);
          int nidx = ny * width + nx;

          color neighbor = input[nidx];

          double spatial_dist_sq = kx * kx + ky * ky;
          double spatial_weight = exp(spatial_dist_sq * spatial_coeff);

          double dr = center.x() - neighbor.x();
          double dg = center.y() - neighbor.y();
          double db = center.z() - neighbor.z();
          double color_dist_sq = dr * dr + dg * dg + db * db;
          double range_weight = exp(color_dist_sq * range_coeff);

          double weight = spatial_weight * range_weight;

          sum = sum + neighbor * weight;
          weight_sum += weight;
        }
      }

      if (weight_sum > 0) {
        output[idx] = sum * (1.0 / weight_sum);
      } else {
        output[idx] = center;
      }
    }
  }

  return output;
#endif
}

bool oidn_denoiser::is_available() {
#ifdef USE_OIDN
  return true;
#else
  return false;
#endif
}

std::string oidn_denoiser::version() {
#ifdef USE_OIDN
  return std::to_string(OIDN_VERSION_MAJOR) + "." +
         std::to_string(OIDN_VERSION_MINOR) + "." +
         std::to_string(OIDN_VERSION_PATCH);
#else
  return "Not available";
#endif
}
