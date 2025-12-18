#ifndef CONFIG_H
#define CONFIG_H

#include "hittable.h"
#include <vector>


// Acceleration method for ray-scene intersection
enum class AccelerationMethod {
  LINEAR, // Test all objects sequentially (original method)
  BVH     // Use Bounding Volume Hierarchy acceleration
};

class config {
public:
  config() {}

public:
  int IMAGE_WIDTH;
  double ASPECT_RATIO;
  int IMAGE_HEIGHT;
  int SAMPLES_PER_PIXEL;
  int MAX_DEPTH;

  // Acceleration structure setting (default to LINEAR for backward
  // compatibility)
  AccelerationMethod acceleration = AccelerationMethod::LINEAR;

  // Enable OIDN AI denoiser (default true if available)
  bool enableDenoiser = true;
};

#endif
