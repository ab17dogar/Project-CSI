#ifndef TRIANGLE_H
#define TRIANGLE_H

#include "../util/vec3.h"
#include "aabb.h"
#include "hittable.h"


/**
 * @brief Triangle primitive with UV texture coordinate support
 *
 * Supports vertex positions (v0, v1, v2) and corresponding UV coordinates
 * (uv0, uv1, uv2) for texture mapping.
 */
class triangle : public hittable {
public:
  triangle() : has_uvs(false) {}

  // Constructor without UVs (backward compatible)
  triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2,
           shared_ptr<material> mat_ptr);

  // Constructor with UVs for texture mapping
  triangle(const vec3 &v0, const vec3 &v1, const vec3 &v2, const vec3 &uv0,
           const vec3 &uv1, const vec3 &uv2, shared_ptr<material> mat_ptr);

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool bounding_box(aabb &output_box) const override;

public:
  // Vertex positions
  vec3 v0, v1, v2;

  // UV texture coordinates (stored as vec3, using x=u, y=v)
  vec3 uv0, uv1, uv2;
  bool has_uvs;

  shared_ptr<material> mat_ptr;
  bool degenerate = false; // true if triangle area is (near) zero
};

#endif
