#ifndef UTIL_H
#define UTIL_H

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <random>

// Usings

using std::make_shared;
using std::shared_ptr;
using std::sqrt;

// Constants

const double EPSILON = 0.00001;
const double INF = std::numeric_limits<double>::infinity();
const double PI = 3.1415926535897932385;
const double INV_PI = 1.0 / PI;
const double INV_2PI = 1.0 / (2.0 * PI);
const double INV_4PI = 1.0 / (4.0 * PI);

// Largest double strictly below 1, so [0,1) samples never round up to 1.
const double ONE_MINUS_EPSILON = 0x1.fffffffffffffp-1;

// Utility Functions

inline double degrees_to_radians(double degrees) {
  return degrees * PI / 180.0;
}

/**
 * @brief PCG32 random number generator (O'Neill 2014).
 *
 * Small state, good statistical quality, and - unlike std::mt19937 seeded
 * from std::random_device - cheap to re-seed for every pixel sample. That is
 * what makes renders bit-for-bit reproducible regardless of how tiles are
 * scheduled across threads.
 */
class pcg32 {
public:
  pcg32() { seed(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL); }
  pcg32(uint64_t init_state, uint64_t init_seq) { seed(init_state, init_seq); }

  void seed(uint64_t init_state, uint64_t init_seq) {
    state = 0u;
    inc = (init_seq << 1u) | 1u;
    next_u32();
    state += init_state;
    next_u32();
  }

  uint32_t next_u32() {
    uint64_t old = state;
    state = old * 6364136223846793005ULL + inc;
    uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = static_cast<uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
  }

  // Uniform double in [0, 1) with 53 bits of randomness.
  double next_double() {
    uint64_t hi = next_u32();
    uint64_t lo = next_u32();
    return static_cast<double>(((hi << 32) | lo) >> 11) * 0x1.0p-53;
  }

private:
  uint64_t state;
  uint64_t inc;
};

// SplitMix64 finalizer: decorrelates structured seeds (pixel / sample ids).
inline uint64_t mix_bits(uint64_t v) {
  v ^= v >> 31;
  v *= 0x7fb5d329728ea185ULL;
  v ^= v >> 27;
  v *= 0x81dadef4bc2dd44dULL;
  v ^= v >> 33;
  return v;
}

inline pcg32 &thread_rng() {
  thread_local pcg32 generator(
      mix_bits(static_cast<uint64_t>(std::random_device{}())),
      mix_bits(static_cast<uint64_t>(std::random_device{}())));
  return generator;
}

/**
 * @brief Re-seed the calling thread's generator for one pixel sample.
 *
 * Every random number drawn while tracing that sample (camera jitter, BSDF
 * and light sampling, Russian roulette, media) then depends only on
 * (pixel, sample, seed), never on thread scheduling.
 */
inline void seed_thread_rng(uint64_t pixel_index, uint64_t sample_index,
                            uint64_t global_seed = 0) {
  thread_rng().seed(mix_bits(pixel_index ^ (global_seed << 40)),
                    mix_bits(sample_index + 0x9e3779b97f4a7c15ULL * global_seed));
}

inline double random_double() { return thread_rng().next_double(); }

inline double random_double(double min, double max) {
  // Returns a random real in [min,max).
  return min + (max - min) * random_double();
}

inline double clamp(double x, double min, double max) {
  if (x < min)
    return min;
  if (x > max)
    return max;
  return x;
}

#endif
