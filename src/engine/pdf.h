#ifndef PDF_H
#define PDF_H

#include "../util/vec3.h"
#include "onb.h"
#include <cmath>
#include <memory>

/**
 * @brief Abstract base class for Probability Density Functions
 *
 * PDFs are used for importance sampling in Monte Carlo integration.
 * They define both how to generate random directions and how to
 * compute the probability of those directions.
 */
class pdf {
public:
  virtual ~pdf() {}

  /**
   * @brief Get the PDF value for a given direction
   * @param direction Direction to evaluate
   * @return Probability density for this direction
   */
  virtual double value(const vec3 &direction) const = 0;

  /**
   * @brief Generate a random direction according to this PDF
   * @return Random direction sampled from the distribution
   */
  virtual vec3 generate() const = 0;
};

/**
 * @brief Cosine-weighted hemisphere PDF for Lambertian materials
 *
 * Generates directions proportional to cos(theta) from surface normal,
 * which is the optimal distribution for Lambertian reflection.
 */
class cosine_pdf : public pdf {
public:
  cosine_pdf(const vec3 &w) { uvw.build_from_w(w); }

  virtual double value(const vec3 &direction) const override {
    auto cosine = dot(unit_vector(direction), uvw.w());
    return (cosine <= 0) ? 0 : cosine / M_PI;
  }

  virtual vec3 generate() const override {
    return uvw.local(random_cosine_direction());
  }

public:
  onb uvw;
};

// Forward declaration
class hittable;

/**
 * @brief PDF targeting a specific hittable object (e.g., a light)
 *
 * Generates directions toward the hittable and computes the
 * probability based on the solid angle of the target.
 */
class hittable_pdf : public pdf {
public:
  hittable_pdf(shared_ptr<hittable> p, const point3 &origin)
      : ptr(p), o(origin) {}

  virtual double value(const vec3 &direction) const override;
  virtual vec3 generate() const override;

public:
  point3 o;
  shared_ptr<hittable> ptr;
};

/**
 * @brief Mixture of two PDFs for combined sampling strategies
 *
 * Randomly chooses between two PDFs with 50/50 probability.
 * Useful for combining material-based and light-based sampling.
 */
class mixture_pdf : public pdf {
public:
  mixture_pdf(shared_ptr<pdf> p0, shared_ptr<pdf> p1) {
    p[0] = p0;
    p[1] = p1;
  }

  virtual double value(const vec3 &direction) const override {
    // Average of both PDFs (since we sample 50/50)
    return 0.5 * p[0]->value(direction) + 0.5 * p[1]->value(direction);
  }

  virtual vec3 generate() const override {
    // 50% chance of using either PDF
    if (random_double() < 0.5)
      return p[0]->generate();
    else
      return p[1]->generate();
  }

public:
  shared_ptr<pdf> p[2];
};

#endif
