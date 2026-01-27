#ifndef MESH_H
#define MESH_H

#include "aabb.h"
#include "hittable.h"
#include "triangle.h"
#include <memory>
#include <string>
#include <vector>


// Forward declaration to avoid circular include
class bvh_node;

class mesh : public hittable {
public:
  mesh(std::string file, vec3 p, vec3 s, vec3 r, std::shared_ptr<material> mat);
  bool load(std::string fileName);
  virtual bool hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const override;
  virtual bool bounding_box(aabb &output_box) const override;

  // Return the number of triangles loaded for diagnostics
  int getTriangleCount() const;

  // Get access to triangles for BVH construction
  const std::vector<triangle> &getTriangles() const { return triangleList; }

  // Build BVH for this mesh - call after load()
  void buildMeshBVH();

  // Check if mesh BVH is built
  bool hasMeshBVH() const { return mesh_bvh != nullptr; }

private:
  std::vector<triangle> triangleList;
  std::shared_ptr<material> material_ptr;

  vec3 position, scale, rotation;
  float angle;
  std::string fileName;

  // Per-mesh BVH for accelerated intersection
  std::shared_ptr<bvh_node> mesh_bvh;

  // Cached bounding box
  mutable aabb cached_box;
  mutable bool box_computed = false;
};

#endif
