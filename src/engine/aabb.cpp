#include "aabb.h"
#include <algorithm>
#include <cmath>

bool aabb::hit(const ray& r, double t_min, double t_max) const {
    // Optimized slab method for ray-box intersection
    // Tests ray against each pair of parallel planes (slabs)
    
    for (int a = 0; a < 3; a++) {
        // Compute inverse direction to avoid division
        double invD = 1.0 / r.direction()[a];
        
        // Compute intersection with the two slabs
        double t0 = (minimum[a] - r.origin()[a]) * invD;
        double t1 = (maximum[a] - r.origin()[a]) * invD;
        
        // Swap if ray direction is negative
        if (invD < 0.0) {
            std::swap(t0, t1);
        }
        
        // Narrow the interval
        t_min = t0 > t_min ? t0 : t_min;
        t_max = t1 < t_max ? t1 : t_max;
        
        // No intersection if interval is empty
        if (t_max <= t_min) {
            return false;
        }
    }
    
    return true;
}

int aabb::longest_axis() const {
    double x = maximum[0] - minimum[0];
    double y = maximum[1] - minimum[1];
    double z = maximum[2] - minimum[2];
    
    if (x > y && x > z) return 0;
    if (y > z) return 1;
    return 2;
}

double aabb::surface_area() const {
    double x = maximum[0] - minimum[0];
    double y = maximum[1] - minimum[1];
    double z = maximum[2] - minimum[2];
    
    return 2.0 * (x * y + y * z + z * x);
}

aabb surrounding_box(const aabb& box0, const aabb& box1) {
    point3 small(
        fmin(box0.min()[0], box1.min()[0]),
        fmin(box0.min()[1], box1.min()[1]),
        fmin(box0.min()[2], box1.min()[2])
    );
    
    point3 big(
        fmax(box0.max()[0], box1.max()[0]),
        fmax(box0.max()[1], box1.max()[1]),
        fmax(box0.max()[2], box1.max()[2])
    );
    
    return aabb(small, big);
}
