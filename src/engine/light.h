#ifndef LIGHT_H
#define LIGHT_H

#include "../util/vec3.h"
#include "hittable.h"
#include "sampling.h"
#include <memory>

class hdri_environment;
class material;
class world;

/**
 * @brief A direction toward a light, drawn for next event estimation.
 */
struct light_sample {
  vec3 wi;            // unit direction from the shading point toward the light
  double dist = INF;  // distance to the sampled point (INF: infinitely far)
  point3 p;           // sampled point on the light (finite lights)
  vec3 n{0, 0, 0};    // light surface normal at p (zero for point lights)
  color li{0, 0, 0};  // radiance arriving along -wi (delta lights: I/d^2, E)
  double pdf = 0.0;   // solid-angle density (delta lights: 1 by convention)
  bool is_delta = false;
  int field_index = -1; // Gaussian the sample came from (Gaussian fields)
};

/**
 * @brief Base class for everything that emits light and can be sampled.
 *
 * Light sampling (sample_li) and the density of BSDF-sampled rays that hit
 * the light (pdf_li) must describe the same distribution. That is what makes
 * the MIS-combined estimator unbiased, and it is exactly what the old
 * integrator got wrong. The test suite checks the two against each other for
 * every light type.
 */
class light {
public:
  enum class kind { area, point, directional, environment };

  explicit light(kind k) : kind_(k) {}
  virtual ~light() = default;

  kind type() const { return kind_; }
  bool is_delta() const {
    return kind_ == kind::point || kind_ == kind::directional;
  }
  bool is_infinite() const {
    return kind_ == kind::directional || kind_ == kind::environment;
  }

  virtual bool sample_li(const point3 &ref, double u1, double u2,
                         light_sample &out) const = 0;

  // Solid-angle density that sample_li(ref) assigns to direction wi. For area
  // lights `hit` is where ray(ref, wi) meets this light; infinite lights
  // ignore it. Delta lights return 0 (they cannot be hit).
  virtual double pdf_li(const point3 &ref, const vec3 &wi,
                        const hit_record *hit) const = 0;

  // Radiance carried by a ray escaping the scene in direction dir.
  virtual color le(const vec3 &dir) const {
    (void)dir;
    return color(0, 0, 0);
  }

  // Luminance of the total emitted power; drives light selection.
  virtual double power() const = 0;

  // Called once the scene bounds are known (infinite lights need them).
  virtual void preprocess(const point3 &center, double radius) {
    (void)center;
    (void)radius;
  }

  int index = -1; // position in world::lights

private:
  kind kind_;
};

/**
 * @brief Diffuse area light attached to an emissive primitive.
 */
class area_light : public light {
public:
  enum class shape { triangle, parallelogram, sphere };

  static std::shared_ptr<area_light>
  make_triangle(const point3 &p0, const point3 &p1, const point3 &p2,
                const material *mat);
  static std::shared_ptr<area_light>
  make_parallelogram(const point3 &q, const vec3 &u, const vec3 &v,
                     const material *mat);
  static std::shared_ptr<area_light>
  make_sphere(const point3 &center, double radius, const material *mat);

  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &ref, const vec3 &wi,
                const hit_record *hit) const override;
  double power() const override;

  shape geometry() const { return shape_; }
  double surface_area() const { return area_; }

  // Radiance leaving surface point p (outward normal n) toward direction w.
  color radiance(const point3 &p, const vec3 &n, const vec3 &w) const;

private:
  area_light() : light(kind::area) {}

  shape shape_ = shape::triangle;
  const material *mat_ = nullptr;
  point3 p0_;     // triangle vertex 0 / parallelogram corner
  vec3 e1_, e2_;  // triangle edges / parallelogram sides
  vec3 n_;        // outward unit normal (planar shapes)
  point3 center_; // sphere
  double radius_ = 0.0;
  double area_ = 0.0;
};

// Isotropic point light; `intensity` is radiant intensity I (W/sr).
class point_light : public light {
public:
  point_light(const point3 &position, const color &intensity)
      : light(kind::point), position_(position), intensity_(intensity) {}

  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &, const vec3 &,
                const hit_record *) const override {
    return 0.0;
  }
  double power() const override { return 4.0 * PI * luminance(intensity_); }

private:
  point3 position_;
  color intensity_;
};

// Distant light (the sun). `to_light` points from the scene toward the
// light, `irradiance` is the irradiance at normal incidence.
class directional_light : public light {
public:
  directional_light(const vec3 &to_light, const color &irradiance)
      : light(kind::directional), dir_(unit_vector(to_light)),
        irradiance_(irradiance) {}

  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &, const vec3 &,
                const hit_record *) const override {
    return 0.0;
  }
  double power() const override {
    return PI * scene_radius_ * scene_radius_ * luminance(irradiance_);
  }
  void preprocess(const point3 &, double radius) override {
    scene_radius_ = radius;
  }

private:
  vec3 dir_;
  color irradiance_;
  double scene_radius_ = 1.0;
};

/**
 * @brief Equirectangular HDR environment, importance sampled by luminance.
 *
 * Directions are drawn from a piecewise-constant distribution over texels
 * weighted by luminance * sin(theta), so bright regions such as the sun in
 * an outdoor HDRI receive proportionally more shadow rays.
 */
class hdri_light : public light {
public:
  explicit hdri_light(std::shared_ptr<hdri_environment> env);

  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &ref, const vec3 &wi,
                const hit_record *hit) const override;
  color le(const vec3 &dir) const override;
  double power() const override;
  void preprocess(const point3 &, double radius) override {
    scene_radius_ = radius;
  }

private:
  std::shared_ptr<hdri_environment> env_;
  sampling::distribution2d distribution_;
  double mean_luminance_ = 0.0;
  double scene_radius_ = 1.0;
};

/**
 * @brief The procedural sky gradient used when no HDRI is given.
 *
 * Reads the sky colors and sun brightness from the world on every lookup,
 * because the GUI edits them live. Radiance depends only on the height y of
 * the direction and is piecewise linear in y, so it is importance sampled
 * exactly by luminance: pick a segment, invert its linear pdf in y, and take
 * the azimuth uniformly (solid angle is dy dphi).
 */
class sky_light : public light {
public:
  explicit sky_light(const world *w) : light(kind::environment), world_(w) {}

  bool sample_li(const point3 &ref, double u1, double u2,
                 light_sample &out) const override;
  double pdf_li(const point3 &ref, const vec3 &wi,
                const hit_record *rec) const override;
  color le(const vec3 &dir) const override;
  double power() const override;
  void preprocess(const point3 &, double radius) override {
    scene_radius_ = radius;
  }

private:
  const world *world_;
  double scene_radius_ = 1.0;
};

// Equirectangular mapping shared by hdri_environment and hdri_light.
// (u, v) in [0,1)^2; v = 0 is straight up (+y).
void direction_to_equirect(const vec3 &dir, double rotation, double &u,
                           double &v);
vec3 equirect_to_direction(double u, double v, double rotation);

#endif
