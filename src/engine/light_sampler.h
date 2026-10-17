#ifndef LIGHT_SAMPLER_H
#define LIGHT_SAMPLER_H

#include "light.h"
#include "sampling.h"
#include <memory>
#include <vector>

/**
 * @brief Chooses which light a next-event-estimation sample goes to.
 *
 * The interface takes the shading point and normal even though the two
 * samplers here ignore them. Spatially aware samplers (light BVHs, and the
 * hierarchy over emissive Gaussians this engine is being built toward) plug in
 * behind the same interface: pmf() must return exactly the probability that
 * sample() would pick `l` at that shading point, or the MIS weights break.
 */
class light_sampler {
public:
  virtual ~light_sampler() = default;
  virtual const light *sample(const point3 &p, const vec3 &n, double u,
                              double &pmf) const = 0;
  virtual double pmf(const point3 &p, const vec3 &n,
                     const light *l) const = 0;
};

class uniform_light_sampler : public light_sampler {
public:
  explicit uniform_light_sampler(std::vector<const light *> lights)
      : lights_(std::move(lights)) {}

  const light *sample(const point3 &, const vec3 &, double u,
                      double &pmf) const override {
    if (lights_.empty())
      return nullptr;
    const size_t n = lights_.size();
    const size_t i = std::min(n - 1, static_cast<size_t>(u * n));
    pmf = 1.0 / static_cast<double>(n);
    return lights_[i];
  }

  double pmf(const point3 &, const vec3 &, const light *l) const override {
    return (l && !lights_.empty()) ? 1.0 / static_cast<double>(lights_.size())
                                   : 0.0;
  }

private:
  std::vector<const light *> lights_;
};

// Picks lights in proportion to emitted power (pbrt's PowerLightSampler).
class power_light_sampler : public light_sampler {
public:
  explicit power_light_sampler(std::vector<const light *> lights)
      : lights_(std::move(lights)) {
    std::vector<double> power;
    power.reserve(lights_.size());
    for (const light *l : lights_)
      power.push_back(l->power());
    if (!power.empty())
      distribution_.build(power);
  }

  const light *sample(const point3 &, const vec3 &, double u,
                      double &pmf) const override {
    if (lights_.empty())
      return nullptr;
    const size_t i = distribution_.sample_discrete(u, pmf);
    return lights_[i];
  }

  double pmf(const point3 &, const vec3 &, const light *l) const override {
    if (!l || l->index < 0 || static_cast<size_t>(l->index) >= lights_.size())
      return 0.0;
    return distribution_.discrete_pmf(static_cast<size_t>(l->index));
  }

private:
  std::vector<const light *> lights_;
  sampling::distribution1d distribution_;
};

#endif
