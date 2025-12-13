#include "bvh_node.h"
#include <algorithm>
#include <iostream>

// Comparator functions for sorting along each axis
bool box_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b, int axis) {
    aabb box_a;
    aabb box_b;

    if (!a->bounding_box(box_a) || !b->bounding_box(box_b)) {
        std::cerr << "No bounding box in bvh_node constructor.\n";
    }

    return box_a.min()[axis] < box_b.min()[axis];
}

bool box_x_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b) {
    return box_compare(a, b, 0);
}

bool box_y_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b) {
    return box_compare(a, b, 1);
}

bool box_z_compare(const std::shared_ptr<hittable>& a, const std::shared_ptr<hittable>& b) {
    return box_compare(a, b, 2);
}

bvh_node::bvh_node(const std::vector<std::shared_ptr<hittable>>& src_objects,
                   size_t start, size_t end) {
    // Make a modifiable copy of the source objects
    auto objects = src_objects;

    // Compute bounding box of all objects to find best split axis
    aabb combined_box;
    bool first = true;
    for (size_t i = start; i < end; i++) {
        aabb temp_box;
        if (objects[i]->bounding_box(temp_box)) {
            combined_box = first ? temp_box : surrounding_box(combined_box, temp_box);
            first = false;
        }
    }
    
    // Choose axis with largest extent for better splits
    int axis = combined_box.longest_axis();
    
    auto comparator = (axis == 0) ? box_x_compare
                    : (axis == 1) ? box_y_compare
                                  : box_z_compare;

    size_t object_span = end - start;

    if (object_span == 1) {
        // Single object - both children point to same object
        left = right = objects[start];
    } else if (object_span == 2) {
        // Two objects - one in each child
        if (comparator(objects[start], objects[start + 1])) {
            left = objects[start];
            right = objects[start + 1];
        } else {
            left = objects[start + 1];
            right = objects[start];
        }
    } else {
        // Multiple objects - sort and split recursively
        std::sort(objects.begin() + start, objects.begin() + end, comparator);

        auto mid = start + object_span / 2;
        left = std::make_shared<bvh_node>(objects, start, mid);
        right = std::make_shared<bvh_node>(objects, mid, end);
    }

    // Compute bounding box for this node
    aabb box_left, box_right;
    if (!left->bounding_box(box_left) || !right->bounding_box(box_right)) {
        std::cerr << "No bounding box in bvh_node constructor.\n";
    }
    box = surrounding_box(box_left, box_right);
}

bool bvh_node::hit(const ray& r, double t_min, double t_max, hit_record& rec) const {
    // First test against this node's bounding box
    if (!box.hit(r, t_min, t_max)) {
        return false;
    }

    // Recursively test children
    bool hit_left = left->hit(r, t_min, t_max, rec);
    bool hit_right = right->hit(r, t_min, hit_left ? rec.t : t_max, rec);

    return hit_left || hit_right;
}

bool bvh_node::bounding_box(aabb& output_box) const {
    output_box = box;
    return true;
}

void bvh_node::countNodes(int& nodes, int& leaves, int depth, int& maxDepth) const {
    nodes++;
    if (depth > maxDepth) {
        maxDepth = depth;
    }
    
    // Check if this is a leaf node (both children are the same object or not bvh_nodes)
    auto left_bvh = std::dynamic_pointer_cast<bvh_node>(left);
    auto right_bvh = std::dynamic_pointer_cast<bvh_node>(right);
    
    if (!left_bvh && !right_bvh) {
        leaves++;
    } else {
        if (left_bvh) {
            left_bvh->countNodes(nodes, leaves, depth + 1, maxDepth);
        } else {
            leaves++;
        }
        if (right_bvh && right_bvh != left_bvh) {
            right_bvh->countNodes(nodes, leaves, depth + 1, maxDepth);
        } else if (!right_bvh && right != left) {
            leaves++;
        }
    }
}

int bvh_node::getNodeCount() const {
    int nodes = 0, leaves = 0, maxDepth = 0;
    countNodes(nodes, leaves, 0, maxDepth);
    return nodes;
}

int bvh_node::getLeafCount() const {
    int nodes = 0, leaves = 0, maxDepth = 0;
    countNodes(nodes, leaves, 0, maxDepth);
    return leaves;
}

int bvh_node::getMaxDepth() const {
    int nodes = 0, leaves = 0, maxDepth = 0;
    countNodes(nodes, leaves, 0, maxDepth);
    return maxDepth;
}
