#ifndef WORLD_H
#define WORLD_H

#include <memory>
#include <mutex>
#include <vector>

#include "config.h"
#include "hittable.h"

class sun;
class camera;
class material;
class bvh_node;
class light;
class light_sampler;

class world : public hittable {
public:
  world();
  ~world() override;

  int GetImageWidth();
  int GetImageHeight();
  double GetAspectRatio();
  int GetSamplesPerPixel();
  int GetMaxDepth();
  AccelerationMethod GetAccelerationMethod() const;

  // Main hit function - dispatches to linear or BVH based on config
  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool hit_any(const ray &r, double t_min, double t_max) const override;
  virtual bool bounding_box(aabb &output_box) const override;

  // Build BVH from current objects (call after scene is loaded)
  void buildBVH();

  // Check if BVH is built
  bool hasBVH() const { return bvh_root != nullptr; }

  /**
   * @brief Finish scene setup before rendering. Idempotent and thread-safe.
   *
   * Builds the BVH (if selected), registers every emissive primitive, point
   * light, the sun and the sky/HDRI as lights, builds the light sampler and
   * derives the ray-offset epsilon from the scene scale.
   */
  void prepare();
  bool isPrepared() const { return prepared; }

  // Offset distance used to spawn rays off surfaces without re-hitting them.
  double rayEpsilon() const { return ray_eps; }

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

  // Filled by prepare().
  std::vector<std::shared_ptr<light>> lights;
  std::vector<const light *> infiniteLights;
  std::unique_ptr<light_sampler> lightSampler;
  point3 sceneCenter{0, 0, 0};
  double sceneRadius = 1.0;

private:
  bool prepared = false;
  double ray_eps = 1e-7;
  std::mutex prepare_mutex;
};

#endif
