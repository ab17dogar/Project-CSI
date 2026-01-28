#ifndef WORLD_H
#define WORLD_H

#include <vector>
// #include <memory>
#include "config.h"
#include "hittable.h"

// #include "sun.h"

class sun;
// class hittable;
class camera;
class material;
class bvh_node;

class world : public hittable {
public:
  world() {}
  int GetImageWidth();
  int GetImageHeight();
  double GetAspectRatio();
  int GetSamplesPerPixel();
  int GetMaxDepth();
  AccelerationMethod GetAccelerationMethod() const;

  // Main hit function - dispatches to linear or BVH based on config
  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool bounding_box(aabb &output_box) const override;

  // Build BVH from current objects (call after scene is loaded)
  void buildBVH();

  // Check if BVH is built
  bool hasBVH() const { return bvh_root != nullptr; }

private:
  // Linear intersection (original method)
  bool hitLinear(const ray &r, double t_min, double t_max,
                 hit_record &rec) const;

  // BVH accelerated intersection
  bool hitBVH(const ray &r, double t_min, double t_max, hit_record &rec) const;

public:
  std::shared_ptr<config> pconfig;
  std::shared_ptr<sun> psun;
  std::shared_ptr<camera> pcamera;
  std::vector<std::shared_ptr<material>> materials;
  std::vector<std::shared_ptr<hittable>> objects;

  // BVH acceleration structure (nullptr if not built)
  std::shared_ptr<bvh_node> bvh_root;

  // HDRI environment map for image-based lighting (optional)
  std::shared_ptr<class hdri_environment> hdri;

  // Point lights for artificial indoor lighting
  std::vector<std::shared_ptr<class PointLight>> pointLights;

  // Dynamic sky colors (for interactive rendering)
  color skyColorTop{0.5, 0.7, 1.0};    // Sky color at zenith
  color skyColorBottom{1.0, 1.0, 1.0}; // Sky color at horizon
  color groundColor{0.5, 0.5, 0.5};    // Ground color (below horizon)
};

#endif
