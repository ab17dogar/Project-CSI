#ifndef INTEGRATOR_H
#define INTEGRATOR_H

#include "../util/ray.h"
#include "config.h"

class world;

struct path_tracer_settings {
  int max_depth = 10;   // scattering events; 0 = emission only, 1 = direct
  int rr_depth = 3;     // bounces before Russian roulette may terminate
  LightStrategy strategy = LightStrategy::MIS;
};

/**
 * @brief Unidirectional path tracer with next event estimation and MIS.
 *
 * At every non-specular vertex the estimator combines two strategies for
 * light arriving from emitters:
 *
 *  - light sampling: pick a light (light_sampler), sample a point on it,
 *    trace a shadow ray;
 *  - BSDF sampling: the continuation ray happens to hit an emitter;
 *
 * weighted with the power heuristic (Veach & Guibas 1995). Delta lights
 * (point, sun) and delta lobes (mirrors, glass) each have a single strategy
 * and take weight 1. Selecting LightStrategy::BSDF or ::NEE disables one
 * strategy. All three settings converge to the same image; the test suite
 * checks this against analytic solutions.
 *
 * `prepare()` must have been called on the world.
 */
color trace_path(const ray &camera_ray, const world &scene,
                 const path_tracer_settings &settings);

path_tracer_settings settings_from_config(const config &cfg);

#endif
