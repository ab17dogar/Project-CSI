#ifndef CONFIG_H
#define CONFIG_H

#include "hittable.h"
#include <cstdint>
#include <vector>

// Acceleration method for ray-scene intersection
enum class AccelerationMethod {
  LINEAR, // Test all objects sequentially (original method)
  BVH     // Use Bounding Volume Hierarchy acceleration
};

// How the path tracer estimates light arriving from emitters.
enum class LightStrategy {
  MIS,  // light sampling + BSDF sampling, combined with the power heuristic
  BSDF, // BSDF sampling only (delta lights still use light sampling)
  NEE   // light sampling only (emitters hit by BSDF rays are ignored)
};

class config {
public:
  config() {}

public:
  int IMAGE_WIDTH;
  double ASPECT_RATIO;
  int IMAGE_HEIGHT;
  int SAMPLES_PER_PIXEL;
  // Maximum number of scattering events along a path. 1 = direct lighting.
  int MAX_DEPTH;

  AccelerationMethod acceleration = AccelerationMethod::BVH;

  LightStrategy lightStrategy = LightStrategy::MIS;
  // Russian roulette starts after this many bounces.
  int rouletteDepth = 3;
  // Seed for the per-pixel random streams; same seed -> same image.
  uint64_t seed = 0;

  // Enable OIDN AI denoiser (default true if available)
  bool enableDenoiser = true;
};

#endif
