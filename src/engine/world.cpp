
#include "world.h"
#include "aabb.h"
#include "bvh_node.h"
#include "config.h"
#include "gaussian_field.h"
#include "hdri_environment.h"
#include "light.h"
#include "light_sampler.h"
#include "point_light.h"
#include "sun.h"
#include "../util/logging.h"
#include <algorithm>
#include <cmath>
#include <iostream>

world::world() = default;
world::~world() = default;

int world::GetImageWidth() { return pconfig->IMAGE_WIDTH; }
int world::GetImageHeight() { return pconfig->IMAGE_HEIGHT; }

double world::GetAspectRatio() { return pconfig->ASPECT_RATIO; }

int world::GetSamplesPerPixel() { return pconfig->SAMPLES_PER_PIXEL; }

int world::GetMaxDepth() { return pconfig->MAX_DEPTH; }

AccelerationMethod world::GetAccelerationMethod() const {
  return pconfig ? pconfig->acceleration : AccelerationMethod::LINEAR;
}

// Main hit function - dispatches based on acceleration method
bool world::hit(const ray &r, double t_min, double t_max,
                hit_record &rec) const {
  if (pconfig && pconfig->acceleration == AccelerationMethod::BVH &&
      bvh_root) {
    return hitBVH(r, t_min, t_max, rec);
  }
  return hitLinear(r, t_min, t_max, rec);
}

bool world::hit_any(const ray &r, double t_min, double t_max) const {
  if (pconfig && pconfig->acceleration == AccelerationMethod::BVH &&
      bvh_root) {
    return bvh_root->hit_any(r, t_min, t_max);
  }
  for (const auto &object : objects) {
    if (object->hit_any(r, t_min, t_max))
      return true;
  }
  return false;
}

// Original linear intersection method
bool world::hitLinear(const ray &r, double t_min, double t_max,
                      hit_record &rec) const {
  hit_record temp_rec;
  bool hit_anything = false;
  auto closest_so_far = t_max;

  for (const auto &object : objects) {
    if (object->hit(r, t_min, closest_so_far, temp_rec)) {
      hit_anything = true;
      closest_so_far = temp_rec.t;
      rec = temp_rec;
    }
  }

  return hit_anything;
}

// BVH accelerated intersection
bool world::hitBVH(const ray &r, double t_min, double t_max,
                   hit_record &rec) const {
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

  if (!g_quiet.load())
    std::cerr << "Building BVH for " << objects.size() << " objects..."
              << std::endl;
  bvh_root = std::make_shared<bvh_node>(objects);
  if (!g_quiet.load())
    std::cerr << "BVH built: " << bvh_root->getNodeCount() << " nodes, "
              << bvh_root->getLeafCount() << " leaves, max depth "
              << bvh_root->getMaxDepth() << std::endl;
}

bool world::bounding_box(aabb &output_box) const {
  if (objects.empty()) {
    return false;
  }

  bool first_box = true;

  for (const auto &object : objects) {
    aabb obj_box;
    if (object->bounding_box(obj_box)) {
      output_box = first_box ? obj_box : surrounding_box(output_box, obj_box);
      first_box = false;
    }
  }

  return !first_box;
}

void world::prepare() {
  std::lock_guard<std::mutex> lock(prepare_mutex);
  if (prepared)
    return;

  if (GetAccelerationMethod() == AccelerationMethod::BVH && !bvh_root &&
      !objects.empty())
    buildBVH();

  // Scene scale: drives the ray epsilon and the power of infinite lights.
  aabb box;
  bool has_box = bounding_box(box);
  if (field && field->size() > 0) {
    const aabb fb = field->world_bounds();
    box = has_box ? surrounding_box(box, fb) : fb;
    has_box = true;
  }
  if (has_box) {
    sceneCenter = 0.5 * (box.min() + box.max());
    sceneRadius = std::max(1e-3, 0.5 * (box.max() - box.min()).length());
    const double extent =
        std::max(max_component(vmax(-box.min(), box.max())), 1.0);
    // Intersections are computed in double precision (~1e-15 relative), so
    // 1e-9 of the scene extent clears self-intersection with a wide margin
    // while staying far below any feature size we care about.
    ray_eps = 1e-9 * extent;
  }

  lights.clear();
  infiniteLights.clear();

  for (auto &object : objects)
    object->collect_lights(lights);

  for (const auto &pl : pointLights)
    lights.push_back(std::make_shared<point_light>(
        pl->position, pl->lightColor * pl->intensity));

  if (psun && luminance(psun->sunColor) > 0.0 &&
      psun->direction.length_squared() > 0.0)
    lights.push_back(
        std::make_shared<directional_light>(psun->direction, psun->sunColor));

  if (hdri && hdri->is_valid()) {
    lights.push_back(std::make_shared<hdri_light>(hdri));
  } else if (psun) {
    const color s = psun->sunColor;
    if (s.x() + s.y() + s.z() >= 0.001)
      lights.push_back(std::make_shared<sky_light>(this));
  }

  std::vector<const light *> raw;
  raw.reserve(lights.size());
  for (size_t i = 0; i < lights.size(); ++i) {
    lights[i]->index = static_cast<int>(i);
    lights[i]->preprocess(sceneCenter, sceneRadius);
    if (lights[i]->is_infinite())
      infiniteLights.push_back(lights[i].get());
    raw.push_back(lights[i].get());
  }
  lightSampler = std::make_unique<power_light_sampler>(std::move(raw));

  if (!g_quiet.load()) {
    size_t area = 0;
    for (const auto &l : lights)
      area += l->type() == light::kind::area ? 1 : 0;
    std::cerr << "Lights: " << lights.size() << " (" << area << " area, "
              << infiniteLights.size() << " infinite)" << std::endl;
    if (field)
      std::cerr << "Gaussian field: " << field->size()
                << " Gaussians, SH degree " << field->sh_degree() << std::endl;
  }

  prepared = true;
}
