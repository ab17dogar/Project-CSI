
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform2.hpp>
#include <iostream>
#include <string>
#include <vector>

#include "../3rdParty/ObjLoader/OBJ_Loader.h"

#include "../util/logging.h"
#include "bvh_node.h"
#include "dielectric.h"
#include "emissive.h"
#include "lambertian.h"
#include "material.h"
#include "mesh.h"
#include "metal.h"
#include "pbr_material.h"

using namespace std;

mesh::mesh(string file, vec3 p, vec3 s, vec3 r, shared_ptr<material> mat) {
  position = p;
  scale = s;
  rotation = r;
  material_ptr = mat;
  fileName = file;
}

int mesh::getTriangleCount() const {
  return static_cast<int>(triangleList.size());
}

// Helper to convert OBJ material to Engine Material
// Helper to convert OBJ material to Engine Material
std::shared_ptr<material> ConvertMaterial(const objl::Material &mat) {
  // 1. Emissive (Light Source)
  // Strict check: if Ke is present, it's an emissive material.
  // We use the raw Ke values from the MTL file.
  if (mat.Ke.X > 0.001f || mat.Ke.Y > 0.001f || mat.Ke.Z > 0.001f) {
    return make_shared<emissive>(color(mat.Ke.X, mat.Ke.Y, mat.Ke.Z));
  }

  // Handle illum models
  int illum = mat.illum;

  // illum 0: Color on and Ambient off
  // illum 1: Color on and Ambient on
  if (illum == 0 || illum == 1) {
    return make_shared<lambertian>(color(mat.Kd.X, mat.Kd.Y, mat.Kd.Z));
  }

  // illum 2: Highlight on (Blinn-Phong) -> Diffuse + Specular
  // illum 3: Reflection on and Ray trace on -> similar to 2 but usually more
  // reflective
  if (illum == 2 || illum == 3) {
    float roughness = 1.0f;
    // Map Shininess (Ns) to Roughness
    if (mat.Ns > 0.0f) {
      roughness = std::sqrt(2.0f / (mat.Ns + 2.0f));
    }

    // Check for PBR metallic map or param
    float metallic = mat.Pm;
    return make_shared<pbr_material>(color(mat.Kd.X, mat.Kd.Y, mat.Kd.Z),
                                     metallic, roughness);
  }

  // illum 4: Transparency: Glass on, Reflection: Ray trace on
  // illum 6: Transparency: Refraction on, Reflection: Fresnel off and Ray trace
  // on illum 7: Transparency: Refraction on, Reflection: Fresnel on and Ray
  // trace on
  if (illum == 4 || illum == 6 || illum == 7) {
    float ior = (mat.Ni > 0.0f) ? mat.Ni : 1.5f;
    // Strict: Use Tf as tint.
    color tint(mat.Tf.X, mat.Tf.Y, mat.Tf.Z);
    // If Tf is zero (invalid for glass usually), default to clear white to
    // avoid invisible object or black hole
    if (mat.Tf.X <= 0.001f && mat.Tf.Y <= 0.001f && mat.Tf.Z <= 0.001f) {
      tint = color(1.0, 1.0, 1.0);
    }
    return make_shared<dielectric>(ior, tint);
  }

  // illum 5: Reflection: Fresnel on and Ray trace on (Mirror)
  if (illum == 5) {
    // Mirror is Metallic = 1.0, Roughness = 0.0.
    // Albedo comes from Ks (Specular Color) because ideal mirrors reflect
    // specularly. Kd is usually 0 for mirrors in OBJ.
    return make_shared<pbr_material>(color(mat.Ks.X, mat.Ks.Y, mat.Ks.Z), 1.0f,
                                     0.0f);
  }

  // Default fallback / illum 8, 9, 10
  // PBR with properties read from file
  color albedo(mat.Kd.X, mat.Kd.Y, mat.Kd.Z);

  float roughness = mat.Pr;
  float metallic = mat.Pm;

  if (roughness == 0.0f && metallic == 0.0f) {
    if (mat.Ns > 0.0f) {
      roughness = std::sqrt(2.0f / (mat.Ns + 2.0f));
    } else {
      roughness = 1.0f;
    }
  }

  return make_shared<pbr_material>(albedo, metallic, roughness);
}

bool mesh::load(string fileName) {
  objl::Loader loader;

  // Try to load the file
  bool success = loader.LoadFile(fileName);

  if (!success) {
    std::cerr << "OBJLoader: Failed to load " << fileName << std::endl;
    return false;
  }

  /*
  if (!g_quiet.load() && !g_suppress_mesh_messages.load()) {
     std::cout << "OBJLoader: Loaded " << loader.LoadedMeshes.size() << "
  sub-meshes from " << fileName << std::endl;
  }
  */

  // Pre-calculate Transform Matrix
  // Note: GLM matrix multiplication is Column-Major, so T * R * S * v
  glm::mat4 T = glm::translate<float>(
      glm::mat4(1.0f), glm::vec3(position.x(), position.y(), position.z()));
  glm::mat4 RX = glm::rotate<float>(glm::mat4(1.0f), rotation.x(),
                                    glm::vec3(1.0f, 0.0f, 0.0f));
  glm::mat4 RY = glm::rotate<float>(glm::mat4(1.0f), rotation.y(),
                                    glm::vec3(0.0f, 1.0f, 0.0f));
  glm::mat4 RZ = glm::rotate<float>(glm::mat4(1.0f), rotation.z(),
                                    glm::vec3(0.0f, 0.0f, 1.0f));
  glm::mat4 S = glm::scale<float>(glm::mat4(1.0f),
                                  glm::vec3(scale.x(), scale.y(), scale.z()));
  glm::mat4 Model = T * RX * RY * RZ * S;

  int total_triangles = 0;

  // OBJLoader splits meshes by material group automatically
  for (const auto &loadedMesh : loader.LoadedMeshes) {

    // Determine Material
    std::shared_ptr<material> mat_for_mesh;

    // If the mesh has a valid material name (not "none"), use it
    if (loadedMesh.MeshMaterial.name != "none" &&
        !loadedMesh.MeshMaterial.name.empty()) {
      mat_for_mesh = ConvertMaterial(loadedMesh.MeshMaterial);
    } else {
      // Fallback to the material assigned in XML/Constructor
      mat_for_mesh = this->material_ptr;
    }

    // Iterate over Indices to form triangles
    // Indices are 0-based relative to the loadedMesh.Vertices array
    for (size_t i = 0; i < loadedMesh.Indices.size(); i += 3) {

      glm::vec4 v[3];
      vec3 uv[3];

      for (int j = 0; j < 3; j++) {
        unsigned int idx = loadedMesh.Indices[i + j];
        const auto &vert = loadedMesh.Vertices[idx];

        // Transform Position
        glm::vec4 worldPos = Model * glm::vec4(vert.Position.X, vert.Position.Y,
                                               vert.Position.Z, 1.0f);
        v[j] = worldPos;

        // UV (Invert Y if necessary, OBJ often has V going up)
        uv[j] = vec3(vert.TextureCoordinate.X, vert.TextureCoordinate.Y, 0.0f);
      }

      // Create Triangle
      triangleList.push_back(triangle(
          vec3(v[0].x, v[0].y, v[0].z), vec3(v[1].x, v[1].y, v[1].z),
          vec3(v[2].x, v[2].y, v[2].z), uv[0], uv[1], uv[2], mat_for_mesh));
      total_triangles++;
    }
  }

  if (!g_quiet.load() && !g_suppress_mesh_messages.load())
    cerr << "Loaded " << total_triangles << " triangles from " << fileName
         << endl;

  // Build BVH
  buildMeshBVH();

  return true;
}

void mesh::buildMeshBVH() {
  if (triangleList.empty()) {
    return;
  }

  std::vector<std::shared_ptr<hittable>> tri_ptrs;
  tri_ptrs.reserve(triangleList.size());

  for (const auto &tri : triangleList) {
    tri_ptrs.push_back(std::make_shared<triangle>(tri));
  }

  mesh_bvh = std::make_shared<bvh_node>(tri_ptrs);

  if (!g_quiet.load() && !g_suppress_mesh_messages.load()) {
    cerr << "Built mesh BVH: " << mesh_bvh->getNodeCount() << " nodes, "
         << mesh_bvh->getLeafCount() << " leaves, max depth "
         << mesh_bvh->getMaxDepth() << endl;
  }
}

bool mesh::hit(const ray &r, double t_min, double t_max,
               hit_record &rec) const {
  if (mesh_bvh) {
    return mesh_bvh->hit(r, t_min, t_max, rec);
  }

  hit_record temp_rec;
  bool hit_anything = false;
  auto closest_so_far = t_max;

  for (size_t i = 0; i < triangleList.size(); i++) {
    if (triangleList[i].hit(r, t_min, closest_so_far, temp_rec)) {
      hit_anything = true;
      closest_so_far = temp_rec.t;
      rec = temp_rec;
    }
  }
  return hit_anything;
}

bool mesh::bounding_box(aabb &output_box) const {
  if (triangleList.empty()) {
    return false;
  }

  if (box_computed) {
    output_box = cached_box;
    return true;
  }

  bool first_box = true;

  for (const auto &tri : triangleList) {
    aabb tri_box;
    if (tri.bounding_box(tri_box)) {
      cached_box = first_box ? tri_box : surrounding_box(cached_box, tri_box);
      first_box = false;
    }
  }

  box_computed = true;
  output_box = cached_box;
  return true;
}
