#ifndef VEC3_H
#define VEC3_H

#include "util.h"
#include <algorithm>
#include <cmath>
#include <iostream>

// All arithmetic is defined inline here. It used to live in vec3.cpp, which
// turned every add, multiply and dot product in the renderer into an
// out-of-line call the optimizer could not see through.
class vec3 {
public:
  vec3() : e{0, 0, 0} {}
  vec3(double e0, double e1, double e2) : e{e0, e1, e2} {}

  double x() const { return e[0]; }
  double y() const { return e[1]; }
  double z() const { return e[2]; }

  bool near_zero() const {
    const double s = 1e-8;
    return (std::fabs(e[0]) < s) && (std::fabs(e[1]) < s) &&
           (std::fabs(e[2]) < s);
  }

  vec3 operator-() const { return vec3(-e[0], -e[1], -e[2]); }
  double operator[](int i) const { return e[i]; }
  double &operator[](int i) { return e[i]; }

  vec3 &operator+=(const vec3 &v) {
    e[0] += v.e[0];
    e[1] += v.e[1];
    e[2] += v.e[2];
    return *this;
  }
  vec3 &operator*=(const vec3 &v) {
    e[0] *= v.e[0];
    e[1] *= v.e[1];
    e[2] *= v.e[2];
    return *this;
  }
  vec3 &operator*=(const double t) {
    e[0] *= t;
    e[1] *= t;
    e[2] *= t;
    return *this;
  }
  vec3 &operator/=(const double t) { return *this *= 1 / t; }

  double length_squared() const {
    return e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
  }
  double length() const { return std::sqrt(length_squared()); }

  inline static vec3 random() {
    return vec3(random_double(), random_double(), random_double());
  }

  inline static vec3 random(double min, double max) {
    return vec3(random_double(min, max), random_double(min, max),
                random_double(min, max));
  }

public:
  double e[3];
};

// Type aliases for vec3
using point3 = vec3; // 3D point
using color = vec3;  // RGB color (linear)

std::ostream &operator<<(std::ostream &out, const vec3 &v);

inline vec3 operator+(const vec3 &u, const vec3 &v) {
  return vec3(u.e[0] + v.e[0], u.e[1] + v.e[1], u.e[2] + v.e[2]);
}
inline vec3 operator-(const vec3 &u, const vec3 &v) {
  return vec3(u.e[0] - v.e[0], u.e[1] - v.e[1], u.e[2] - v.e[2]);
}
inline vec3 operator*(const vec3 &u, const vec3 &v) {
  return vec3(u.e[0] * v.e[0], u.e[1] * v.e[1], u.e[2] * v.e[2]);
}
inline vec3 operator*(double t, const vec3 &v) {
  return vec3(t * v.e[0], t * v.e[1], t * v.e[2]);
}
inline vec3 operator*(const vec3 &v, double t) { return t * v; }
inline vec3 operator/(const vec3 &v, double t) { return (1 / t) * v; }
inline vec3 operator/(const vec3 &u, const vec3 &v) {
  return vec3(u.e[0] / v.e[0], u.e[1] / v.e[1], u.e[2] / v.e[2]);
}

inline double dot(const vec3 &u, const vec3 &v) {
  return u.e[0] * v.e[0] + u.e[1] * v.e[1] + u.e[2] * v.e[2];
}
inline double abs_dot(const vec3 &u, const vec3 &v) {
  return std::fabs(dot(u, v));
}
inline vec3 cross(const vec3 &u, const vec3 &v) {
  return vec3(u.e[1] * v.e[2] - u.e[2] * v.e[1],
              u.e[2] * v.e[0] - u.e[0] * v.e[2],
              u.e[0] * v.e[1] - u.e[1] * v.e[0]);
}
inline vec3 unit_vector(const vec3 &v) { return v / v.length(); }

inline vec3 vmin(const vec3 &a, const vec3 &b) {
  return vec3(std::min(a.e[0], b.e[0]), std::min(a.e[1], b.e[1]),
              std::min(a.e[2], b.e[2]));
}
inline vec3 vmax(const vec3 &a, const vec3 &b) {
  return vec3(std::max(a.e[0], b.e[0]), std::max(a.e[1], b.e[1]),
              std::max(a.e[2], b.e[2]));
}
inline double max_component(const vec3 &v) {
  return std::max(v.e[0], std::max(v.e[1], v.e[2]));
}
inline double min_component(const vec3 &v) {
  return std::min(v.e[0], std::min(v.e[1], v.e[2]));
}
inline vec3 lerp(double t, const vec3 &a, const vec3 &b) {
  return (1.0 - t) * a + t * b;
}

// Rec. 709 relative luminance of a linear RGB color.
inline double luminance(const color &c) {
  return 0.2126 * c.e[0] + 0.7152 * c.e[1] + 0.0722 * c.e[2];
}
inline bool is_black(const color &c) {
  return c.e[0] == 0.0 && c.e[1] == 0.0 && c.e[2] == 0.0;
}
inline bool is_finite(const vec3 &v) {
  return std::isfinite(v.e[0]) && std::isfinite(v.e[1]) &&
         std::isfinite(v.e[2]);
}

// Mirror v about n (v points toward the surface, as in Ray Tracing in One
// Weekend).
inline vec3 reflect(const vec3 &v, const vec3 &n) {
  return v - 2 * dot(v, n) * n;
}

vec3 refract(const vec3 &uv, const vec3 &n, double etai_over_etat);
vec3 random_in_unit_sphere();
vec3 random_unit_vector();
vec3 random_in_hemisphere(const vec3 &normal);

// Cosine-weighted hemisphere sampling for importance sampling
vec3 random_cosine_direction();

// Random point in unit disk for depth of field
vec3 random_in_unit_disk();

#endif
