#include "bvh_node.h"

bvh_node::bvh_node(const std::vector<std::shared_ptr<hittable>> &src_objects,
                   size_t start, size_t end) {
  std::vector<std::shared_ptr<hittable>> bounded;
  std::vector<aabb> boxes;
  for (size_t i = start; i < end; ++i) {
    aabb b;
    if (src_objects[i]->bounding_box(b)) {
      bounded.push_back(src_objects[i]);
      boxes.push_back(b);
    } else {
      unbounded.push_back(src_objects[i]);
    }
  }

  bvh.build(boxes, 4);
  prims.reserve(bounded.size());
  for (uint32_t idx : bvh.order())
    prims.push_back(bounded[idx]);

  if (!bvh.empty()) {
    box = bvh.bounds();
    has_box = unbounded.empty();
  }
}

bool bvh_node::hit(const ray &r, double t_min, double t_max,
                   hit_record &rec) const {
  bool hit_anything = false;
  hit_record tmp;
  double closest = t_max;
  for (const auto &obj : unbounded) {
    if (obj->hit(r, t_min, closest, tmp)) {
      hit_anything = true;
      closest = tmp.t;
      rec = tmp;
    }
  }
  hit_anything |= bvh.traverse(
      r, t_min, closest, false,
      [&](uint32_t first, uint32_t count, double &t_far) {
        bool any = false;
        for (uint32_t i = first; i < first + count; ++i) {
          if (prims[i]->hit(r, t_min, t_far, tmp)) {
            any = true;
            t_far = tmp.t;
            rec = tmp;
          }
        }
        return any;
      });
  return hit_anything;
}

bool bvh_node::hit_any(const ray &r, double t_min, double t_max) const {
  for (const auto &obj : unbounded) {
    if (obj->hit_any(r, t_min, t_max))
      return true;
  }
  return bvh.traverse(r, t_min, t_max, true,
                      [&](uint32_t first, uint32_t count, double &t_far) {
                        for (uint32_t i = first; i < first + count; ++i) {
                          if (prims[i]->hit_any(r, t_min, t_far))
                            return true;
                        }
                        return false;
                      });
}

bool bvh_node::bounding_box(aabb &output_box) const {
  output_box = box;
  return has_box;
}

void bvh_node::collect_lights(std::vector<std::shared_ptr<light>> &out) {
  for (auto &p : prims)
    p->collect_lights(out);
  for (auto &p : unbounded)
    p->collect_lights(out);
}

int bvh_node::getNodeCount() const { return static_cast<int>(bvh.node_count()); }

int bvh_node::getLeafCount() const { return static_cast<int>(bvh.leaf_count()); }

int bvh_node::getMaxDepth() const { return bvh.max_depth(); }
