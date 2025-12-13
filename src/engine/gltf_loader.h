#ifndef GLTF_LOADER_H
#define GLTF_LOADER_H

#include "../util/vec3.h"
#include "hittable.h"
#include "image_texture.h"
#include "material.h"
#include "pbr_material.h"
#include "texture.h"
#include "triangle.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

/**
 * @brief glTF 2.0 model loader
 *
 * Loads glTF/GLB files with:
 * - Mesh geometry (triangles)
 * - PBR materials (metallic-roughness workflow)
 * - Textures (albedo, normal, metallic-roughness)
 */
class gltf_loader {
public:
  struct LoadResult {
    std::vector<std::shared_ptr<hittable>> objects;
    std::vector<std::shared_ptr<material>> materials;
    std::vector<std::shared_ptr<texture>> textures;
    bool success = false;
    std::string error;
  };

  /**
   * @brief Load a glTF or GLB file
   *
   * @param filename Path to .gltf or .glb file
   * @param position World position offset
   * @param scale Uniform scale
   * @param rotation Euler rotation (radians)
   * @return LoadResult with objects and materials
   */
  static LoadResult load(const std::string &filename,
                         const vec3 &position = vec3(0, 0, 0),
                         const vec3 &scale = vec3(1, 1, 1),
                         const vec3 &rotation = vec3(0, 0, 0));

  /**
   * @brief Check if a file is glTF format
   */
  static bool is_gltf_file(const std::string &filename);
};

#endif
