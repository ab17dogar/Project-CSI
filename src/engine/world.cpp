
#include "world.h"
#include "config.h"
#include "bvh_node.h"
#include "aabb.h"
#include <iostream>

int world::GetImageWidth(){
    return pconfig->IMAGE_WIDTH;
}
int world::GetImageHeight(){
    return pconfig->IMAGE_HEIGHT;
}

double world::GetAspectRatio(){
    return pconfig->ASPECT_RATIO;
}

int world::GetSamplesPerPixel(){
    return pconfig->SAMPLES_PER_PIXEL;
}

int world::GetMaxDepth(){
    return pconfig->MAX_DEPTH;
}

AccelerationMethod world::GetAccelerationMethod() const {
    return pconfig ? pconfig->acceleration : AccelerationMethod::LINEAR;
}

// Main hit function - dispatches based on acceleration method
bool world::hit(const ray& r, double t_min, double t_max, hit_record& rec) const {
    if (pconfig && pconfig->acceleration == AccelerationMethod::BVH && bvh_root) {
        return hitBVH(r, t_min, t_max, rec);
    }
    return hitLinear(r, t_min, t_max, rec);
}

// Original linear intersection method
bool world::hitLinear(const ray& r, double t_min, double t_max, hit_record& rec) const {
    hit_record temp_rec;
    bool hit_anything = false;
    auto closest_so_far = t_max;

    for (const auto& object : objects) {
        if (object->hit(r, t_min, closest_so_far, temp_rec)) {
            hit_anything = true;
            closest_so_far = temp_rec.t;
            rec = temp_rec;
        }
    }

    return hit_anything;
}

// BVH accelerated intersection
bool world::hitBVH(const ray& r, double t_min, double t_max, hit_record& rec) const {
    if (!bvh_root) {
        return hitLinear(r, t_min, t_max, rec);
    }
    return bvh_root->hit(r, t_min, t_max, rec);
}

// Build BVH from current objects
void world::buildBVH() {
    if (objects.empty()) {
        std::cerr << "Warning: Cannot build BVH - no objects in scene" << std::endl;
        return;
    }
    
    std::cerr << "Building BVH for " << objects.size() << " objects..." << std::endl;
    bvh_root = std::make_shared<bvh_node>(objects);
    std::cerr << "BVH built: " << bvh_root->getNodeCount() << " nodes, " 
              << bvh_root->getLeafCount() << " leaves, max depth " 
              << bvh_root->getMaxDepth() << std::endl;
}

bool world::bounding_box(aabb& output_box) const {
    if (objects.empty()) {
        return false;
    }
    
    aabb temp_box;
    bool first_box = true;
    
    for (const auto& object : objects) {
        aabb obj_box;
        if (object->bounding_box(obj_box)) {
            output_box = first_box ? obj_box : surrounding_box(output_box, obj_box);
            first_box = false;
        }
    }
    
    return !first_box;
}
