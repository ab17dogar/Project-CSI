#include "bvh.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct box3 {
  double lo[3] = {INF, INF, INF};
  double hi[3] = {-INF, -INF, -INF};

  void grow(const double *mn, const double *mx) {
    for (int a = 0; a < 3; ++a) {
      lo[a] = std::min(lo[a], mn[a]);
      hi[a] = std::max(hi[a], mx[a]);
    }
  }
  void grow(const double *p) { grow(p, p); }
  void grow(const box3 &b) { grow(b.lo, b.hi); }
  bool valid() const { return lo[0] <= hi[0]; }
  double area() const {
    if (!valid())
      return 0.0;
    const double dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    return 2.0 * (dx * dy + dy * dz + dz * dx);
  }
  int longest_axis() const {
    const double dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
    if (dx > dy && dx > dz)
      return 0;
    return dy > dz ? 1 : 2;
  }
};

constexpr int kBins = 16;
// Relative cost of a traversal step versus a primitive test (pbrt uses 1/8).
constexpr double kTraversalCost = 0.125;

} // namespace

void flat_bvh::build(const std::vector<aabb> &prim_bounds, int max_leaf_size) {
  nodes_.clear();
  order_.clear();
  max_depth_ = 0;
  max_leaf_ = std::max(1, max_leaf_size);
  if (prim_bounds.empty())
    return;

  std::vector<build_prim> prims(prim_bounds.size());
  for (size_t i = 0; i < prim_bounds.size(); ++i) {
    build_prim &p = prims[i];
    for (int a = 0; a < 3; ++a) {
      p.bmin[a] = prim_bounds[i].min()[a];
      p.bmax[a] = prim_bounds[i].max()[a];
      p.centroid[a] = 0.5 * (p.bmin[a] + p.bmax[a]);
    }
    p.index = static_cast<uint32_t>(i);
  }
  nodes_.reserve(2 * prims.size());
  order_.reserve(prims.size());
  build_recursive(prims, 0, prims.size(), 0);
}

uint32_t flat_bvh::build_recursive(std::vector<build_prim> &prims,
                                   size_t begin, size_t end, int depth) {
  const uint32_t node_index = static_cast<uint32_t>(nodes_.size());
  nodes_.emplace_back();
  max_depth_ = std::max(max_depth_, depth);

  box3 bounds, centroids;
  for (size_t i = begin; i < end; ++i) {
    bounds.grow(prims[i].bmin, prims[i].bmax);
    centroids.grow(prims[i].centroid);
  }
  const size_t count = end - begin;

  auto finish = [&](uint32_t offset, uint32_t n, uint32_t axis) {
    node &nd = nodes_[node_index];
    for (int a = 0; a < 3; ++a) {
      nd.bmin[a] = bounds.lo[a];
      nd.bmax[a] = bounds.hi[a];
    }
    nd.offset = offset;
    nd.count = n;
    nd.axis = axis;
    return node_index;
  };
  auto make_leaf = [&]() {
    const uint32_t first = static_cast<uint32_t>(order_.size());
    for (size_t i = begin; i < end; ++i)
      order_.push_back(prims[i].index);
    return finish(first, static_cast<uint32_t>(count), 0);
  };

  if (count <= 1)
    return make_leaf();

  const int axis = centroids.longest_axis();
  const double cmin = centroids.lo[axis];
  const double extent = centroids.hi[axis] - cmin;

  size_t mid = begin;
  auto median_split = [&]() {
    mid = begin + count / 2;
    std::nth_element(prims.begin() + begin, prims.begin() + mid,
                     prims.begin() + end,
                     [axis](const build_prim &a, const build_prim &b) {
                       return a.centroid[axis] < b.centroid[axis];
                     });
  };

  if (!(extent > 0.0)) {
    // All centroids coincide: SAH cannot separate them.
    if (count <= static_cast<size_t>(max_leaf_))
      return make_leaf();
    mid = begin + count / 2;
  } else if (depth >= kSahDepthLimit) {
    median_split();
  } else {
    // Binned SAH.
    struct bin {
      box3 box;
      size_t n = 0;
    } bins[kBins];
    const double scale = kBins / extent;
    auto bin_of = [&](const build_prim &p) {
      int b = static_cast<int>((p.centroid[axis] - cmin) * scale);
      return std::clamp(b, 0, kBins - 1);
    };
    for (size_t i = begin; i < end; ++i) {
      bin &b = bins[bin_of(prims[i])];
      b.box.grow(prims[i].bmin, prims[i].bmax);
      ++b.n;
    }
    // Sweep from the right to get suffix areas, then from the left.
    double right_area[kBins];
    size_t right_count[kBins];
    box3 acc;
    size_t acc_n = 0;
    for (int b = kBins - 1; b > 0; --b) {
      acc.grow(bins[b].box);
      acc_n += bins[b].n;
      right_area[b] = acc.area();
      right_count[b] = acc_n;
    }
    const double node_area = bounds.area();
    double best_cost = std::numeric_limits<double>::infinity();
    int best_split = -1;
    box3 left;
    size_t left_n = 0;
    for (int b = 0; b < kBins - 1; ++b) {
      left.grow(bins[b].box);
      left_n += bins[b].n;
      if (left_n == 0 || right_count[b + 1] == 0)
        continue;
      const double cost =
          node_area > 0.0
              ? kTraversalCost + (left_n * left.area() +
                                  right_count[b + 1] * right_area[b + 1]) /
                                     node_area
              : kTraversalCost + 0.5 * static_cast<double>(count);
      if (cost < best_cost) {
        best_cost = cost;
        best_split = b;
      }
    }

    const double leaf_cost = static_cast<double>(count);
    if (count <= static_cast<size_t>(max_leaf_) &&
        (best_split < 0 || leaf_cost <= best_cost))
      return make_leaf();

    if (best_split < 0) {
      median_split();
    } else {
      auto it = std::partition(
          prims.begin() + begin, prims.begin() + end,
          [&](const build_prim &p) { return bin_of(p) <= best_split; });
      mid = static_cast<size_t>(it - prims.begin());
      if (mid == begin || mid == end)
        median_split();
    }
  }

  build_recursive(prims, begin, mid, depth + 1); // first child = node_index+1
  const uint32_t second = build_recursive(prims, mid, end, depth + 1);
  return finish(second, 0, static_cast<uint32_t>(axis));
}

size_t flat_bvh::leaf_count() const {
  size_t n = 0;
  for (const node &nd : nodes_)
    n += nd.count > 0 ? 1 : 0;
  return n;
}

aabb flat_bvh::bounds() const {
  if (nodes_.empty())
    return aabb(point3(0, 0, 0), point3(0, 0, 0));
  const node &r = nodes_[0];
  return aabb(point3(r.bmin[0], r.bmin[1], r.bmin[2]),
              point3(r.bmax[0], r.bmax[1], r.bmax[2]));
}
