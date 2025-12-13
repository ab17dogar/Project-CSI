#ifndef SPHERE_H
#define SPHERE_H

#include "hittable.h"
#include "aabb.h"

class sphere : public hittable {
    public:
        sphere() {}
        sphere(point3 cen, double r, shared_ptr<material> m) 
                : center(cen), radius(r), mat_ptr(m) {};

        virtual bool hit(const ray& r, double t_min, double t_max, hit_record& rec) const override;
        virtual bool bounding_box(aabb& output_box) const override;

    public:
        point3 center;
        double radius;
        shared_ptr<material> mat_ptr;
};


#endif
