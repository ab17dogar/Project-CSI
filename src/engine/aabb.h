#ifndef AABB_H
#define AABB_H

#include "../util/vec3.h"
#include "../util/ray.h"

// Axis-Aligned Bounding Box for BVH acceleration
class aabb {
public:
    aabb() {}
    aabb(const point3& a, const point3& b) : minimum(a), maximum(b) {}

    point3 min() const { return minimum; }
    point3 max() const { return maximum; }

    // Fast ray-box intersection test using the slab method
    bool hit(const ray& r, double t_min, double t_max) const;

    // Get the longest axis (0=x, 1=y, 2=z) - useful for BVH split decisions
    int longest_axis() const;

    // Surface area of the box - useful for SAH (Surface Area Heuristic)
    double surface_area() const;

public:
    point3 minimum;
    point3 maximum;
};

// Compute the bounding box that surrounds two boxes
aabb surrounding_box(const aabb& box0, const aabb& box1);

#endif
