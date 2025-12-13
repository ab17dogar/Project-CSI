#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE

#include "gltf_loader.h"
#include "../3rdParty/tiny_gltf.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>

namespace {

// Build transformation matrix
glm::mat4 buildTransform(const vec3 &pos, const vec3 &scale, const vec3 &rot) {
  glm::mat4 T =
      glm::translate(glm::mat4(1.0f), glm::vec3(pos.x(), pos.y(), pos.z()));
  glm::mat4 RX = glm::rotate(glm::mat4(1.0f), static_cast<float>(rot.x()),
                             glm::vec3(1, 0, 0));
  glm::mat4 RY = glm::rotate(glm::mat4(1.0f), static_cast<float>(rot.y()),
                             glm::vec3(0, 1, 0));
  glm::mat4 RZ = glm::rotate(glm::mat4(1.0f), static_cast<float>(rot.z()),
                             glm::vec3(0, 0, 1));
  glm::mat4 S =
      glm::scale(glm::mat4(1.0f), glm::vec3(scale.x(), scale.y(), scale.z()));
  return T * RX * RY * RZ * S;
}

} // namespace

gltf_loader::LoadResult gltf_loader::load(const std::string &filename,
                                          const vec3 &position,
                                          const vec3 &scale,
                                          const vec3 &rotation) {
  LoadResult result;

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string err, warn;

  // Determine if binary or ASCII
  bool binary =
      filename.size() > 4 && filename.substr(filename.size() - 4) == ".glb";

  bool success;
  if (binary) {
    success = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
  } else {
    success = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
  }

  if (!warn.empty()) {
    std::cerr << "glTF Warning: " << warn << std::endl;
  }

  if (!success) {
    result.error = err.empty() ? "Failed to load glTF file" : err;
    std::cerr << "glTF Error: " << result.error << std::endl;
    return result;
  }

  std::cerr << "Loaded glTF: " << filename << std::endl;
  std::cerr << "  Meshes: " << model.meshes.size() << std::endl;
  std::cerr << "  Materials: " << model.materials.size() << std::endl;
  std::cerr << "  Textures: " << model.textures.size() << std::endl;

  // Build transformation matrix
  glm::mat4 transform = buildTransform(position, scale, rotation);

  // Load materials
  std::vector<std::shared_ptr<material>> materials;
  for (const auto &gltfMat : model.materials) {
    // Extract PBR metallic-roughness properties
    auto &pbr = gltfMat.pbrMetallicRoughness;

    color baseColor(pbr.baseColorFactor[0], pbr.baseColorFactor[1],
                    pbr.baseColorFactor[2]);

    float metallic = static_cast<float>(pbr.metallicFactor);
    float roughness = static_cast<float>(pbr.roughnessFactor);

    auto mat = std::make_shared<pbr_material>(baseColor, metallic, roughness);
    materials.push_back(mat);
    result.materials.push_back(mat);

    std::cerr << "  Material: " << gltfMat.name << " (M:" << metallic
              << ", R:" << roughness << ")" << std::endl;
  }

  // Default material if none defined
  if (materials.empty()) {
    materials.push_back(
        std::make_shared<pbr_material>(color(0.8, 0.8, 0.8), 0.0f, 0.5f));
  }

  // Load meshes
  for (const auto &mesh : model.meshes) {
    for (const auto &primitive : mesh.primitives) {
      // Get indices
      std::vector<uint32_t> indices;
      if (primitive.indices >= 0) {
        const auto &accessor = model.accessors[primitive.indices];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto &buffer = model.buffers[bufferView.buffer];
        const uint8_t *data =
            buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

        for (size_t i = 0; i < accessor.count; i++) {
          uint32_t idx = 0;
          switch (accessor.componentType) {
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
            idx = reinterpret_cast<const uint16_t *>(data)[i];
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
            idx = reinterpret_cast<const uint32_t *>(data)[i];
            break;
          case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
            idx = data[i];
            break;
          }
          indices.push_back(idx);
        }
      }

      // Get positions
      std::vector<glm::vec3> positions;
      auto posIt = primitive.attributes.find("POSITION");
      if (posIt != primitive.attributes.end()) {
        const auto &accessor = model.accessors[posIt->second];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto &buffer = model.buffers[bufferView.buffer];
        const float *data = reinterpret_cast<const float *>(
            buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

        for (size_t i = 0; i < accessor.count; i++) {
          positions.emplace_back(data[i * 3], data[i * 3 + 1], data[i * 3 + 2]);
        }
      }

      // Get texture coordinates
      std::vector<glm::vec2> texcoords;
      auto uvIt = primitive.attributes.find("TEXCOORD_0");
      if (uvIt != primitive.attributes.end()) {
        const auto &accessor = model.accessors[uvIt->second];
        const auto &bufferView = model.bufferViews[accessor.bufferView];
        const auto &buffer = model.buffers[bufferView.buffer];
        const float *data = reinterpret_cast<const float *>(
            buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);

        for (size_t i = 0; i < accessor.count; i++) {
          texcoords.emplace_back(data[i * 2], data[i * 2 + 1]);
        }
      }

      // Get material
      int matIdx = primitive.material >= 0 ? primitive.material : 0;
      matIdx = std::min(matIdx, static_cast<int>(materials.size()) - 1);
      auto mat = materials[matIdx];

      // Create triangles
      if (indices.empty()) {
        // Non-indexed geometry
        for (size_t i = 0; i + 2 < positions.size(); i += 3) {
          glm::vec4 v0 = transform * glm::vec4(positions[i], 1.0f);
          glm::vec4 v1 = transform * glm::vec4(positions[i + 1], 1.0f);
          glm::vec4 v2 = transform * glm::vec4(positions[i + 2], 1.0f);

          vec3 uv0(0, 0, 0), uv1(0, 0, 0), uv2(0, 0, 0);
          if (!texcoords.empty()) {
            uv0 = vec3(texcoords[i].x, texcoords[i].y, 0);
            uv1 = vec3(texcoords[i + 1].x, texcoords[i + 1].y, 0);
            uv2 = vec3(texcoords[i + 2].x, texcoords[i + 2].y, 0);
          }

          result.objects.push_back(std::make_shared<triangle>(
              vec3(v0.x, v0.y, v0.z), vec3(v1.x, v1.y, v1.z),
              vec3(v2.x, v2.y, v2.z), uv0, uv1, uv2, mat));
        }
      } else {
        // Indexed geometry
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
          size_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];

          if (i0 >= positions.size() || i1 >= positions.size() ||
              i2 >= positions.size())
            continue;

          glm::vec4 v0 = transform * glm::vec4(positions[i0], 1.0f);
          glm::vec4 v1 = transform * glm::vec4(positions[i1], 1.0f);
          glm::vec4 v2 = transform * glm::vec4(positions[i2], 1.0f);

          vec3 uv0(0, 0, 0), uv1(0, 0, 0), uv2(0, 0, 0);
          if (!texcoords.empty() && i0 < texcoords.size() &&
              i1 < texcoords.size() && i2 < texcoords.size()) {
            uv0 = vec3(texcoords[i0].x, texcoords[i0].y, 0);
            uv1 = vec3(texcoords[i1].x, texcoords[i1].y, 0);
            uv2 = vec3(texcoords[i2].x, texcoords[i2].y, 0);
          }

          result.objects.push_back(std::make_shared<triangle>(
              vec3(v0.x, v0.y, v0.z), vec3(v1.x, v1.y, v1.z),
              vec3(v2.x, v2.y, v2.z), uv0, uv1, uv2, mat));
        }
      }
    }
  }

  std::cerr << "  Created " << result.objects.size() << " triangles"
            << std::endl;
  result.success = true;
  return result;
}

bool gltf_loader::is_gltf_file(const std::string &filename) {
  if (filename.size() < 5)
    return false;
  std::string ext = filename.substr(filename.size() - 5);
  std::string ext4 = filename.substr(filename.size() - 4);
  return ext == ".gltf" || ext4 == ".glb";
}
