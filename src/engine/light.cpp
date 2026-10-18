#include "light.h"

#include <algorithm>
#include <cmath>

#include "hdri_environment.h"
#include "material.h"
#include "onb.h"
#include "sun.h"
#include "world.h"

// --------------------------------------------------------------------------
// Equirectangular mapping (kept identical to the original hdri lookup)
// --------------------------------------------------------------------------

void direction_to_equirect(const vec3 &dir, double rotation, double &u,
                           double &v) {
  vec3 d = dir;
  if (rotation != 0.0) {
    const double c = std::cos(rotation), s = std::sin(rotation);
    d = vec3(dir.x() * c - dir.z() * s, dir.y(), dir.x() * s + dir.z() * c);
  }
  const double theta = std::acos(std::clamp(d.y(), -1.0, 1.0)); // 0..pi
  const double phi = std::atan2(d.z(), d.x());                   // -pi..pi
  u = std::clamp((phi + PI) * INV_2PI, 0.0, ONE_MINUS_EPSILON);
  v = std::clamp(theta * INV_PI, 0.0, ONE_MINUS_EPSILON);
}

vec3 equirect_to_direction(double u, double v, double rotation) {
  const double theta = v * PI;
  const double phi = u * 2.0 * PI - PI;
  const double st = std::sin(theta);
  const vec3 d(st * std::cos(phi), std::cos(theta), st * std::sin(phi));
  if (rotation == 0.0)
    return d;
  const double c = std::cos(rotation), s = std::sin(rotation);
  // Inverse of the rotation applied in direction_to_equirect.
  return vec3(d.x() * c + d.z() * s, d.y(), -d.x() * s + d.z() * c);
}

// --------------------------------------------------------------------------
// Area lights
// --------------------------------------------------------------------------

std::shared_ptr<area_light> area_light::make_triangle(const point3 &p0,
                                                      const point3 &p1,
                                                      const point3 &p2,
                                                      const material *mat) {
  std::shared_ptr<area_light> l(new area_light());
  l->shape_ = shape::triangle;
  l->mat_ = mat;
  l->p0_ = p0;
  l->e1_ = p1 - p0;
  l->e2_ = p2 - p0;
  const vec3 c = cross(l->e1_, l->e2_);
  l->area_ = 0.5 * c.length();
  l->n_ = l->area_ > 0.0 ? unit_vector(c) : vec3(0, 0, 1);
  return l;
}

std::shared_ptr<area_light>
area_light::make_parallelogram(const point3 &q, const vec3 &u, const vec3 &v,
                               const material *mat) {
  std::shared_ptr<area_light> l(new area_light());
  l->shape_ = shape::parallelogram;
  l->mat_ = mat;
  l->p0_ = q;
  l->e1_ = u;
  l->e2_ = v;
  const vec3 c = cross(u, v);
  l->area_ = c.length();
  l->n_ = l->area_ > 0.0 ? unit_vector(c) : vec3(0, 0, 1);
  return l;
}

std::shared_ptr<area_light> area_light::make_sphere(const point3 &center,
                                                    double radius,
                                                    const material *mat) {
  std::shared_ptr<area_light> l(new area_light());
  l->shape_ = shape::sphere;
  l->mat_ = mat;
  l->center_ = center;
  l->radius_ = radius;
  l->area_ = 4.0 * PI * radius * radius;
  return l;
}

color area_light::radiance(const point3 &p, const vec3 &n,
                           const vec3 &w) const {
  // Build the hit record the emitter would see for a ray arriving along -w,
  // so emission (including one-sidedness) matches a BSDF-sampled hit exactly.
  hit_record rec;
  rec.p = p;
  rec.front_face = dot(w, n) > 0.0;
  rec.normal = rec.front_face ? n : -n;
  rec.mat_ptr = mat_;
  return mat_->emitted(rec, w);
}

bool area_light::sample_li(const point3 &ref, double u1, double u2,
                           light_sample &out) const {
  point3 p;
  vec3 n;
  double pdf_area_or_solid;
  bool solid_angle = false;

  switch (shape_) {
  case shape::triangle: {
    double b0, b1;
    sampling::square_to_triangle(u1, u2, b0, b1);
    const double b2 = 1.0 - b0 - b1;
    p = p0_ + b1 * e1_ + b2 * e2_;
    n = n_;
    pdf_area_or_solid = 1.0 / area_;
    break;
  }
  case shape::parallelogram:
    p = p0_ + u1 * e1_ + u2 * e2_;
    n = n_;
    pdf_area_or_solid = 1.0 / area_;
    break;
  case shape::sphere: {
    const vec3 to_center = center_ - ref;
    const double dc2 = to_center.length_squared();
    const double r2 = radius_ * radius_;
    if (dc2 <= r2 * (1.0 + 1e-9)) {
      // Inside (or on) the sphere: uniform area sampling.
      n = sampling::square_to_uniform_sphere(u1, u2);
      p = center_ + radius_ * n;
      pdf_area_or_solid = 1.0 / area_;
    } else {
      // Outside: sample the cone of directions subtended by the sphere.
      const double dc = std::sqrt(dc2);
      const double sin2_max = r2 / dc2;
      const double cos_max = std::sqrt(std::max(0.0, 1.0 - sin2_max));
      const double one_minus_cos_max = sin2_max / (1.0 + cos_max);
      const vec3 local =
          sampling::square_to_uniform_cone(u1, u2, one_minus_cos_max);
      const vec3 wi = unit_vector(onb(to_center / dc).local(local));
      const double sin2_t = local.x() * local.x() + local.y() * local.y();
      const double t =
          dc * local.z() - std::sqrt(std::max(0.0, r2 - dc2 * sin2_t));
      p = ref + t * wi;
      n = unit_vector(p - center_);
      pdf_area_or_solid = sampling::uniform_cone_pdf(one_minus_cos_max);
      solid_angle = true;
    }
    break;
  }
  }

  const vec3 d = p - ref;
  const double dist2 = d.length_squared();
  if (dist2 <= 0.0)
    return false;
  const double dist = std::sqrt(dist2);
  out.wi = d / dist;
  out.dist = dist;
  out.p = p;
  out.n = n;
  out.is_delta = false;
  if (solid_angle) {
    out.pdf = pdf_area_or_solid;
  } else {
    const double cos_l = std::fabs(dot(n, out.wi));
    if (cos_l < 1e-12)
      return false;
    out.pdf = pdf_area_or_solid * dist2 / cos_l;
  }
  out.li = radiance(p, n, -out.wi);
  return out.pdf > 0.0 && std::isfinite(out.pdf);
}

double area_light::pdf_li(const point3 &ref, const vec3 &wi,
                          const hit_record *hit) const {
  if (shape_ == shape::sphere) {
    const vec3 to_center = center_ - ref;
    const double dc2 = to_center.length_squared();
    const double r2 = radius_ * radius_;
    if (dc2 > r2 * (1.0 + 1e-9)) {
      const double sin2_max = r2 / dc2;
      const double cos_max = std::sqrt(std::max(0.0, 1.0 - sin2_max));
      return sampling::uniform_cone_pdf(sin2_max / (1.0 + cos_max));
    }
  }
  if (!hit)
    return 0.0;
  const vec3 n = shape_ == shape::sphere ? unit_vector(hit->p - center_) : n_;
  const double cos_l = std::fabs(dot(n, wi));
  if (cos_l < 1e-12)
    return 0.0;
  return (hit->p - ref).length_squared() / (cos_l * area_);
}

double area_light::power() const {
  // Lambertian emitter: pi * A * L per emitting side.
  const vec3 n = shape_ == shape::sphere ? vec3(0, 0, 1) : n_;
  const point3 p = shape_ == shape::sphere ? center_ + radius_ * n : p0_;
  double sides = luminance(radiance(p, n, n));
  if (shape_ != shape::sphere)
    sides += luminance(radiance(p, n, -n));
  return PI * area_ * sides;
}

// --------------------------------------------------------------------------
// Delta lights
// --------------------------------------------------------------------------

bool point_light::sample_li(const point3 &ref, double u1, double u2,
                            light_sample &out) const {
  (void)u1;
  (void)u2;
  const vec3 d = position_ - ref;
  const double dist2 = d.length_squared();
  if (dist2 <= 0.0)
    return false;
  out.dist = std::sqrt(dist2);
  out.wi = d / out.dist;
  out.p = position_;
  out.n = vec3(0, 0, 0);
  out.li = intensity_ / dist2;
  out.pdf = 1.0;
  out.is_delta = true;
  return true;
}

bool directional_light::sample_li(const point3 &ref, double u1, double u2,
                                  light_sample &out) const {
  (void)ref;
  (void)u1;
  (void)u2;
  out.wi = dir_;
  out.dist = INF;
  out.li = irradiance_;
  out.pdf = 1.0;
  out.is_delta = true;
  return true;
}

// --------------------------------------------------------------------------
// HDR environment
// --------------------------------------------------------------------------

hdri_light::hdri_light(std::shared_ptr<hdri_environment> env)
    : light(kind::environment), env_(std::move(env)) {
  const int w = env_->get_width();
  const int h = env_->get_height();
  std::vector<double> weights(static_cast<size_t>(w) * h);
  double lum_sum = 0.0;
  double solid_angle_sum = 0.0;
  for (int j = 0; j < h; ++j) {
    const double sin_theta = std::sin(PI * (j + 0.5) / h);
    for (int i = 0; i < w; ++i) {
      const double lum = std::max(0.0, luminance(env_->texel(i, j)));
      weights[static_cast<size_t>(j) * w + i] = lum * sin_theta;
      lum_sum += lum * sin_theta;
      solid_angle_sum += sin_theta;
    }
  }
  distribution_.build(weights, w, h);
  mean_luminance_ = solid_angle_sum > 0.0 ? lum_sum / solid_angle_sum : 0.0;
}

color hdri_light::le(const vec3 &dir) const { return env_->sample(dir); }

bool hdri_light::sample_li(const point3 &ref, double u1, double u2,
                           light_sample &out) const {
  (void)ref;
  double u, v, pdf_uv;
  distribution_.sample(u1, u2, u, v, pdf_uv);
  if (!(pdf_uv > 0.0))
    return false;
  const double sin_theta = std::sin(v * PI);
  if (sin_theta <= 0.0)
    return false;
  out.wi = equirect_to_direction(u, v, env_->rotation);
  out.dist = INF;
  out.pdf = pdf_uv / (2.0 * PI * PI * sin_theta);
  out.li = env_->sample(out.wi);
  out.is_delta = false;
  return true;
}

double hdri_light::pdf_li(const point3 &ref, const vec3 &wi,
                          const hit_record *hit) const {
  (void)ref;
  (void)hit;
  double u, v;
  direction_to_equirect(wi, env_->rotation, u, v);
  const double sin_theta = std::sin(v * PI);
  if (sin_theta <= 0.0)
    return 0.0;
  return distribution_.pdf(u, v) / (2.0 * PI * PI * sin_theta);
}

double hdri_light::power() const {
  return PI * scene_radius_ * scene_radius_ * mean_luminance_;
}

// --------------------------------------------------------------------------
// Procedural sky (the original gradient, unchanged in appearance)
// --------------------------------------------------------------------------

color sky_light::le(const vec3 &dir) const {
  const color sun_col = world_->psun ? world_->psun->sunColor : color(0, 0, 0);
  const double sun_brightness = sun_col.x() + sun_col.y() + sun_col.z();
  if (sun_brightness < 0.001)
    return color(0, 0, 0);
  const double y = unit_vector(dir).y();
  color result;
  if (y > 0.0) {
    result = (1.0 - y) * world_->skyColorBottom + y * world_->skyColorTop;
  } else {
    const double t = std::min(1.0, -y * 2.0);
    result = (1.0 - t) * world_->skyColorBottom + t * world_->groundColor;
  }
  return result * std::min(1.0, sun_brightness / 3.0);
}

namespace {

// Luminance of the sky as a function of y: linear from the ground color at
// y = -0.5 to the horizon color at y = 0 and on to the zenith color at
// y = 1, constant ground color below y = -0.5 (see sky_light::le).
struct sky_profile {
  // Segments in y, with the luminance at both ends.
  double y0[3] = {-1.0, -0.5, 0.0};
  double y1[3] = {-0.5, 0.0, 1.0};
  double f0[3], f1[3];
  double mass[3]; // integral of luminance over the segment in y
  double total = 0.0;

  explicit sky_profile(const sky_light &sky) {
    const double ground = luminance(sky.le(vec3(0, -1, 0)));
    const double horizon = luminance(sky.le(vec3(1, 0, 0)));
    const double zenith = luminance(sky.le(vec3(0, 1, 0)));
    const double f[4] = {ground, ground, horizon, zenith};
    for (int s = 0; s < 3; ++s) {
      f0[s] = f[s];
      f1[s] = f[s + 1];
      mass[s] = 0.5 * (f0[s] + f1[s]) * (y1[s] - y0[s]);
      total += mass[s];
    }
  }

  double luminance_at(double y) const {
    for (int s = 0; s < 3; ++s)
      if (y <= y1[s] || s == 2) {
        const double t = std::clamp((y - y0[s]) / (y1[s] - y0[s]), 0.0, 1.0);
        return (1.0 - t) * f0[s] + t * f1[s];
      }
    return 0.0;
  }
};

} // namespace

bool sky_light::sample_li(const point3 &ref, double u1, double u2,
                          light_sample &out) const {
  (void)ref;
  const sky_profile p(*this);
  if (p.total <= 0.0)
    return false;
  // Choose a segment by its mass, then reuse u1 within it.
  double u = u1 * p.total;
  int s = 0;
  while (s < 2 && u >= p.mass[s]) {
    u -= p.mass[s];
    ++s;
  }
  if (p.mass[s] <= 0.0)
    return false;
  u = std::clamp(u / p.mass[s], 0.0, 1.0);
  // Invert the CDF of the linear pdf f0 (1 - t) + f1 t on [0, 1]. The
  // rationalized root is stable for f0 = f1 and for f0 = 0.
  const double f0 = p.f0[s], f1 = p.f1[s];
  const double denom = f0 + std::sqrt(std::max(0.0, f0 * f0 + (f1 * f1 - f0 * f0) * u));
  const double t = denom > 0.0 ? std::clamp(u * (f0 + f1) / denom, 0.0, 1.0) : u;
  const double y = p.y0[s] + t * (p.y1[s] - p.y0[s]);
  const double r = std::sqrt(std::max(0.0, 1.0 - y * y));
  const double phi = 2.0 * PI * u2;
  out.wi = vec3(r * std::cos(phi), y, r * std::sin(phi));
  out.dist = INF;
  out.pdf = p.luminance_at(y) / (2.0 * PI * p.total);
  out.li = le(out.wi);
  out.is_delta = false;
  return out.pdf > 0.0;
}

double sky_light::pdf_li(const point3 &, const vec3 &wi,
                         const hit_record *) const {
  const sky_profile p(*this);
  if (p.total <= 0.0)
    return 0.0;
  return p.luminance_at(unit_vector(wi).y()) / (2.0 * PI * p.total);
}

double sky_light::power() const {
  // Mean luminance over the sphere: the integral over y and phi of the
  // luminance, divided by 4 pi.
  const double mean = sky_profile(*this).total * 2.0 * PI / (4.0 * PI);
  return PI * scene_radius_ * scene_radius_ * mean;
}
