#include "integrator.h"

#include <algorithm>
#include <cmath>

#include "light.h"
#include "light_sampler.h"
#include "material.h"
#include "sampling.h"
#include "world.h"

namespace {

// Ray origin nudged off the surface toward the side `w` leaves through.
// Volume scattering points are not on a surface and are used as-is.
point3 spawn_origin(const hit_record &rec, const vec3 &w, double eps) {
  if (rec.mat_ptr && rec.mat_ptr->is_phase_function())
    return rec.p;
  return rec.p + (dot(w, rec.normal) > 0.0 ? eps : -eps) * rec.normal;
}

/**
 * Visibility ray from the shading point to a light sample.
 *
 * For finite lights the ray is aimed at the sampled point itself, nudged off
 * the light surface toward the shading point, and stops just short of it.
 * Re-using the sampled direction from an offset origin instead (a parallel
 * line) lets the shadow ray clip the emitter near its silhouette; for a
 * sphere light that wrongly shadowed ~5% of samples, which the analytic
 * sphere test caught.
 */
bool shadow_ray(const hit_record &rec, const light_sample &ls, double eps,
                ray &out, double &t_max) {
  const point3 o = spawn_origin(rec, ls.wi, eps);
  if (std::isinf(ls.dist)) {
    out = ray(o, ls.wi);
    t_max = INF;
    return true;
  }
  point3 target = ls.p;
  if (ls.n.length_squared() > 0.0)
    target = ls.p + (dot(ls.n, o - ls.p) > 0.0 ? eps : -eps) * ls.n;
  const vec3 d = target - o;
  const double len = d.length();
  if (!(len > 0.0))
    return false;
  out = ray(o, d / len);
  t_max = len * (1.0 - 1e-9);
  return true;
}

} // namespace

ray_counters &thread_ray_counters() {
  thread_local ray_counters counters;
  return counters;
}

path_tracer_settings settings_from_config(const config &cfg) {
  path_tracer_settings s;
  s.max_depth = cfg.MAX_DEPTH;
  s.rr_depth = cfg.rouletteDepth;
  s.strategy = cfg.lightStrategy;
  return s;
}

color trace_path(const ray &camera_ray, const world &scene,
                 const path_tracer_settings &settings) {
  const double eps = scene.rayEpsilon();
  const double t_min = 0.1 * eps;
  const light_sampler *lights = scene.lightSampler.get();
  const bool use_light_sampling = settings.strategy != LightStrategy::BSDF;
  const bool use_bsdf_hits = settings.strategy != LightStrategy::NEE;
  const bool combine = settings.strategy == LightStrategy::MIS;

  color L(0, 0, 0);
  color beta(1, 1, 1);
  double eta_scale = 1.0;

  // State of the previous vertex, for MIS on emitters hit by BSDF rays.
  bool specular_bounce = true; // camera rays count as specular
  double prev_bsdf_pdf = 0.0;
  point3 prev_p;
  vec3 prev_n;

  ray r(camera_ray.origin(), unit_vector(camera_ray.direction()),
        camera_ray.time());

  ray_counters &counters = thread_ray_counters();

  for (int depth = 0;; ++depth) {
    hit_record rec;
    if (depth == 0)
      ++counters.camera;
    else
      ++counters.bounce;
    if (!scene.hit(r, t_min, INF, rec)) {
      // Escaped: collect environment emission.
      for (const light *env : scene.infiniteLights) {
        const color le = env->le(r.direction());
        if (is_black(le))
          continue;
        if (specular_bounce) {
          L += beta * le;
        } else if (combine) {
          const double light_pdf = lights->pmf(prev_p, prev_n, env) *
                                   env->pdf_li(prev_p, r.direction(), nullptr);
          L += beta * le *
               sampling::power_heuristic(prev_bsdf_pdf, light_pdf);
        } else if (use_bsdf_hits) {
          L += beta * le;
        }
      }
      break;
    }

    const vec3 wo = -r.direction();
    const material *mat = rec.mat_ptr;

    // Emission at this vertex.
    if (mat->is_emissive()) {
      const color le = mat->emitted(rec, wo);
      if (!is_black(le)) {
        if (specular_bounce || rec.area_light == nullptr) {
          // No light-sampling strategy covers this path: full weight.
          L += beta * le;
        } else if (combine) {
          const double light_pdf =
              lights->pmf(prev_p, prev_n, rec.area_light) *
              rec.area_light->pdf_li(prev_p, r.direction(), &rec);
          L += beta * le *
               sampling::power_heuristic(prev_bsdf_pdf, light_pdf);
        } else if (use_bsdf_hits) {
          L += beta * le;
        }
      }
    }

    if (depth >= settings.max_depth || !mat->scatters())
      break;

    // Next event estimation.
    if (lights && mat->has_smooth_lobes()) {
      double pmf = 0.0;
      const light *l = lights->sample(rec.p, rec.normal, random_double(), pmf);
      if (l && pmf > 0.0 && (use_light_sampling || l->is_delta())) {
        const double u1 = random_double();
        const double u2 = random_double();
        light_sample ls;
        ray shadow;
        double t_max = INF;
        if (l->sample_li(rec.p, u1, u2, ls) && !is_black(ls.li)) {
          const double cos_i =
              mat->is_phase_function() ? 1.0 : abs_dot(ls.wi, rec.normal);
          const color f = mat->eval(rec, wo, ls.wi) * cos_i;
          if (!is_black(f) && shadow_ray(rec, ls, eps, shadow, t_max) &&
              (++counters.shadow, !scene.hit_any(shadow, t_min, t_max))) {
            const double light_pdf = pmf * ls.pdf;
            double w = 1.0;
            if (!ls.is_delta && combine)
              w = sampling::power_heuristic(light_pdf,
                                            mat->pdf(rec, wo, ls.wi));
            L += beta * f * ls.li * (w / light_pdf);
          }
        }
      } else if (l && !use_light_sampling) {
        // BSDF-only mode: still consume the numbers so random streams line
        // up across strategies at the same seed.
        random_double();
        random_double();
      }
    }

    // Continue the path by sampling the BSDF.
    bsdf_sample bs;
    const double uc = random_double();
    const double u1 = random_double();
    const double u2 = random_double();
    if (!mat->sample(rec, wo, uc, u1, u2, bs))
      break;
    if (is_black(bs.weight) || !is_finite(bs.weight))
      break;

    beta *= bs.weight;
    if (bs.is_transmission)
      eta_scale *= bs.eta * bs.eta;
    specular_bounce = bs.is_delta;
    prev_bsdf_pdf = bs.pdf;
    prev_p = rec.p;
    prev_n = rec.normal;

    // Russian roulette on the throughput, with the 1/eta^2 radiance scaling
    // factored out so paths inside glass are not culled for being "dim".
    if (depth + 1 >= settings.rr_depth) {
      const double q = std::max(0.05, 1.0 - max_component(beta * eta_scale));
      if (random_double() < q)
        break;
      beta /= (1.0 - q);
    }

    r = ray(spawn_origin(rec, bs.wi, eps), bs.wi, r.time());
  }

  return L;
}
