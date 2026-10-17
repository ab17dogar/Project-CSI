#ifndef BVH_H
#define BVH_H

#include "../util/ray.h"
#include "aabb.h"
#include <cstdint>
#include <vector>

/**
 * @brief Flattened bounding volume hierarchy built with the binned surface
 * area heuristic (Wald 2007; pbrt-v3 sec. 4.3).
 *
 * The hierarchy only stores indices. Callers keep their primitives in
 * order() so that each leaf covers a contiguous range, and test them in a
 * callback. That way one implementation serves the scene-level BVH (over
 * polymorphic objects) and per-mesh BVHs (over triangles stored by value,
 * with no virtual call per triangle).
 *
 * Nodes are laid out depth-first: an interior node's first child follows it
 * immediately, and `offset` holds the second child. Traversal is iterative
 * and visits the nearer child first.
 *
 * This replaces the old pointer-based median-split tree, which copied the
 * entire object list at every node (O(n^2) build) and, despite the README,
 * did not use the SAH.
 */
class flat_bvh {
public:
  struct node {
    double bmin[3];
    double bmax[3];
    uint32_t offset; // leaf: first index into order(); interior: 2nd child
    uint32_t count;  // leaf: number of primitives; 0 for interior nodes
    uint32_t axis;   // split axis (interior)
  };

  // Build over primitive bounds. Afterwards order()[k] is the original index
  // of the primitive at position k.
  void build(const std::vector<aabb> &prim_bounds, int max_leaf_size = 4);

  bool empty() const { return nodes_.empty(); }
  const std::vector<uint32_t> &order() const { return order_; }
  const std::vector<node> &nodes() const { return nodes_; }
  size_t node_count() const { return nodes_.size(); }
  size_t leaf_count() const;
  int max_depth() const { return max_depth_; }
  aabb bounds() const;

  /**
   * Visit the leaves whose boxes the ray overlaps within [t_min, t_max].
   * `leaf(first, count, t_max)` tests positions [first, first + count) and
   * returns true on a hit, shrinking t_max for closest-hit queries. With
   * any_hit the traversal stops at the first leaf that reports a hit.
   */
  template <class LeafFn>
  bool traverse(const ray &r, double t_min, double t_max, bool any_hit,
                LeafFn &&leaf) const {
    if (nodes_.empty())
      return false;
    const vec3 o = r.origin();
    const vec3 d = r.direction();
    const double inv[3] = {1.0 / d.x(), 1.0 / d.y(), 1.0 / d.z()};
    const bool neg[3] = {inv[0] < 0.0, inv[1] < 0.0, inv[2] < 0.0};
    const double org[3] = {o.x(), o.y(), o.z()};

    uint32_t stack[kStackSize];
    int sp = 0;
    uint32_t idx = 0;
    bool hit = false;
    while (true) {
      const node &n = nodes_[idx];
      if (overlaps(n, org, inv, t_min, t_max)) {
        if (n.count > 0) {
          if (leaf(n.offset, n.count, t_max)) {
            hit = true;
            if (any_hit)
              return true;
          }
          if (sp == 0)
            break;
          idx = stack[--sp];
        } else if (neg[n.axis]) {
          stack[sp++] = idx + 1;
          idx = n.offset;
        } else {
          stack[sp++] = n.offset;
          idx = idx + 1;
        }
      } else {
        if (sp == 0)
          break;
        idx = stack[--sp];
      }
    }
    return hit;
  }

private:
  // Depth is capped during the build (median splits past kSahDepthLimit), so
  // this bounds the traversal stack.
  static constexpr int kStackSize = 128;
  static constexpr int kSahDepthLimit = 48;

  struct build_prim {
    double bmin[3], bmax[3];
    double centroid[3];
    uint32_t index;
  };

  static bool overlaps(const node &n, const double *o, const double *inv,
                       double t0, double t1) {
    // Robust slab test: widen the far distance by a few ulps (pbrt's
    // gamma(3)) so rounding never culls a box the ray actually grazes.
    constexpr double kWiden = 1.0 + 2.0 * 3.3306690738754716e-16;
    for (int a = 0; a < 3; ++a) {
      double tn = (n.bmin[a] - o[a]) * inv[a];
      double tf = (n.bmax[a] - o[a]) * inv[a];
      if (tn > tf) {
        double tmp = tn;
        tn = tf;
        tf = tmp;
      }
      tf *= kWiden;
      // Written so a NaN (0 * inf for a ray lying in a slab plane) leaves
      // the interval unchanged instead of poisoning it.
      t0 = tn > t0 ? tn : t0;
      t1 = tf < t1 ? tf : t1;
      if (t0 > t1)
        return false;
    }
    return true;
  }

  uint32_t build_recursive(std::vector<build_prim> &prims, size_t begin,
                           size_t end, int depth);

  std::vector<node> nodes_;
  std::vector<uint32_t> order_;
  int max_depth_ = 0;
  int max_leaf_ = 4;
};

#endif
