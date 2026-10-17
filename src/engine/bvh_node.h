#ifndef BVH_NODE_H
#define BVH_NODE_H

#include <memory>
#include <vector>

#include "aabb.h"
#include "bvh.h"
#include "hittable.h"

/**
 * @brief Scene-level BVH over polymorphic objects (spheres, quads, meshes...).
 *
 * Thin wrapper around flat_bvh. Objects without a finite bounding box are
 * kept in a side list and tested against every ray.
 */
class bvh_node : public hittable {
public:
  bvh_node() {}

  // Build BVH from a list of objects
  bvh_node(const std::vector<std::shared_ptr<hittable>> &src_objects,
           size_t start, size_t end);

  // Convenience constructor that builds from entire list
  bvh_node(const std::vector<std::shared_ptr<hittable>> &objects)
      : bvh_node(objects, 0, objects.size()) {}

  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool hit_any(const ray &r, double t_min, double t_max) const override;
  virtual bool bounding_box(aabb &output_box) const override;
  virtual void
  collect_lights(std::vector<std::shared_ptr<light>> &out) override;

  // Get statistics about the BVH tree
  int getNodeCount() const;
  int getLeafCount() const;
  int getMaxDepth() const;

private:
  flat_bvh bvh;
  std::vector<std::shared_ptr<hittable>> prims;     // in BVH order
  std::vector<std::shared_ptr<hittable>> unbounded; // no finite box
  aabb box;
  bool has_box = false;
};

#endif
