#ifndef DENOISER_H
#define DENOISER_H

#include "../util/vec3.h"
#include <vector>
#include <cmath>
#include <algorithm>

/**
 * @brief Simple image denoiser using bilateral filtering
 * 
 * Reduces noise while preserving edges. No external library required.
 * Good for quick previews; for production use OIDN would be better.
 */
namespace denoiser {

/**
 * @brief Apply bilateral filter to denoise rendered image
 * 
 * @param input Input image as flat color array (width * height)
 * @param width Image width
 * @param height Image height
 * @param sigma_spatial Spatial sigma (larger = more blur)
 * @param sigma_range Range sigma (larger = less edge preservation)
 * @param kernel_size Filter kernel size (must be odd)
 * @return Denoised image
 */
inline std::vector<color> bilateral_filter(
    const std::vector<color>& input,
    int width, int height,
    double sigma_spatial = 2.0,
    double sigma_range = 0.1,
    int kernel_size = 5)
{
    std::vector<color> output(width * height);
    int half_kernel = kernel_size / 2;
    
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
                    
                    // Spatial weight (distance-based)
                    double spatial_dist_sq = kx * kx + ky * ky;
                    double spatial_weight = exp(spatial_dist_sq * spatial_coeff);
                    
                    // Range weight (color similarity)
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
}

/**
 * @brief Apply simple box blur (faster but less quality)
 */
inline std::vector<color> box_blur(
    const std::vector<color>& input,
    int width, int height,
    int kernel_size = 3)
{
    std::vector<color> output(width * height);
    int half_kernel = kernel_size / 2;
    double inv_count = 1.0 / (kernel_size * kernel_size);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            color sum(0, 0, 0);
            
            for (int ky = -half_kernel; ky <= half_kernel; ky++) {
                for (int kx = -half_kernel; kx <= half_kernel; kx++) {
                    int nx = std::clamp(x + kx, 0, width - 1);
                    int ny = std::clamp(y + ky, 0, height - 1);
                    sum = sum + input[ny * width + nx];
                }
            }
            
            output[y * width + x] = sum * inv_count;
        }
    }
    
    return output;
}

/**
 * @brief Median filter for removing outlier fireflies
 */
inline std::vector<color> median_filter(
    const std::vector<color>& input,
    int width, int height,
    int kernel_size = 3)
{
    std::vector<color> output(width * height);
    int half_kernel = kernel_size / 2;
    int sample_count = kernel_size * kernel_size;
    
    std::vector<double> r_vals(sample_count);
    std::vector<double> g_vals(sample_count);
    std::vector<double> b_vals(sample_count);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int i = 0;
            
            for (int ky = -half_kernel; ky <= half_kernel; ky++) {
                for (int kx = -half_kernel; kx <= half_kernel; kx++) {
                    int nx = std::clamp(x + kx, 0, width - 1);
                    int ny = std::clamp(y + ky, 0, height - 1);
                    color c = input[ny * width + nx];
                    r_vals[i] = c.x();
                    g_vals[i] = c.y();
                    b_vals[i] = c.z();
                    i++;
                }
            }
            
            std::sort(r_vals.begin(), r_vals.end());
            std::sort(g_vals.begin(), g_vals.end());
            std::sort(b_vals.begin(), b_vals.end());
            
            int mid = sample_count / 2;
            output[y * width + x] = color(r_vals[mid], g_vals[mid], b_vals[mid]);
        }
    }
    
    return output;
}

} // namespace denoiser

#endif
