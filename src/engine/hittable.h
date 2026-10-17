#ifndef HITTABLE_H
#define HITTABLE_H

#include "../util/ray.h"
#include <memory>
#include <vector>

class material;
class aabb;
class light;

struct hit_record {
  point3 p;
  // Unit geometric normal, flipped to face the incoming ray. front_face says
  // whether that is the primitive's outward side.
  vec3 normal;
  // Raw pointer: hit records are created for every candidate intersection,
  // and copying a shared_ptr there costs an atomic increment that all render
  // threads contend on. Primitives own their materials for the scene lifetime.
  const material *mat_ptr = nullptr;
  // The light this surface is registered as, if it is an emitter that the
  // integrator can sample directly (needed for the MIS weight on BSDF hits).
  const light *area_light = nullptr;
  double t = 0.0;
  double u = 0.0; // Texture U coordinate
  double v = 0.0; // Texture V coordinate
  bool front_face = true;

  inline void set_face_normal(const ray &r, const vec3 &outward_normal) {
    front_face = dot(r.direction(), outward_normal) < 0;
    normal = front_face ? outward_normal : -outward_normal;
  }
};

class hittable {
public:
  virtual ~hittable() = default;

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const = 0;

  // Occlusion query: is there any intersection in (t_min, t_max)? Shadow rays
  // only need a yes/no answer, so acceleration structures override this to
  // stop at the first hit instead of searching for the closest one.
  virtual bool hit_any(const ray &r, double t_min, double t_max) const {
    hit_record rec;
    return hit(r, t_min, t_max, rec);
  }

  // Compute bounding box for BVH acceleration
  // Returns true if the object has a finite bounding box, false otherwise
  virtual bool bounding_box(aabb &output_box) const = 0;

  // Register every emissive primitive as a light the integrator can sample
  // (next event estimation). Primitives keep a pointer to their light so that
  // BSDF-sampled hits can evaluate the light-sampling pdf for MIS. Emitters
  // that are not registered are still rendered, just without light sampling.
  virtual void collect_lights(std::vector<std::shared_ptr<light>> &out) {
    (void)out;
  }
};

#endif
