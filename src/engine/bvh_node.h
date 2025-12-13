#ifndef BVH_NODE_H
#define BVH_NODE_H

#include <vector>
#include <memory>
#include "hittable.h"
#include "aabb.h"

// BVH (Bounding Volume Hierarchy) acceleration structure
// Organizes scene objects into a binary tree of bounding boxes
// for efficient ray-scene intersection testing
class bvh_node : public hittable {
public:
    bvh_node() {}
    
    // Build BVH from a list of objects
    bvh_node(const std::vector<std::shared_ptr<hittable>>& src_objects,
             size_t start, size_t end);
    
    // Convenience constructor that builds from entire list
    bvh_node(const std::vector<std::shared_ptr<hittable>>& objects)
        : bvh_node(objects, 0, objects.size()) {}
    
    virtual bool hit(const ray& r, double t_min, double t_max, hit_record& rec) const override;
    virtual bool bounding_box(aabb& output_box) const override;
    
    // Get statistics about the BVH tree
    int getNodeCount() const;
    int getLeafCount() const;
    int getMaxDepth() const;

private:
    std::shared_ptr<hittable> left;
    std::shared_ptr<hittable> right;
    aabb box;
    
    // Helper functions for tree statistics
    void countNodes(int& nodes, int& leaves, int depth, int& maxDepth) const;
};

// Comparator functions for sorting objects along each axis
bool box_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b, int axis);
bool box_x_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b);
bool box_y_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b);
bool box_z_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b);

#endif
