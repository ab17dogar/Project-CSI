
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/transform2.hpp>
#include <iostream>


#include "../3rdParty/ObjLoader/OBJ_Loader.h"
#include "../util/logging.h"
#include "mesh.h"


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

bool mesh::load(string fileName) {

  objl::Loader loader;

  // Load .obj File
  bool loadout = loader.LoadFile(fileName);
  if (!loadout) {
    if (!g_quiet.load() && !g_suppress_mesh_messages.load())
      cerr << "Failed to open: " << fileName << endl;
    return false;
  } else {
    if (!g_quiet.load() && !g_suppress_mesh_messages.load())
      cerr << "Success opening: " << fileName << endl;

    for (size_t i = 0; i < loader.LoadedMeshes.size(); i++) {
      // Copy one of the loaded meshes to be our current mesh
      objl::Mesh pmesh = loader.LoadedMeshes[i];

      if (pmesh.Vertices.size() < 3) {
        cerr << "Loaded mesh has fewer than 3 vertices, skipping: " << fileName
             << endl;
        continue;
      }

      // Build transformation matrix
      glm::mat4 T = glm::translate<float>(
          glm::mat4(1.0f), glm::vec3(position.x(), position.y(), position.z()));
      glm::mat4 RX = glm::rotate<float>(glm::mat4(1.0f), rotation.x(),
                                        glm::vec3(1.0f, 0.0f, 0.0f));
      glm::mat4 RY = glm::rotate<float>(glm::mat4(1.0f), rotation.y(),
                                        glm::vec3(0.0f, 1.0f, 0.0f));
      glm::mat4 RZ = glm::rotate<float>(glm::mat4(1.0f), rotation.z(),
                                        glm::vec3(0.0f, 0.0f, 1.0f));
      glm::mat4 S = glm::scale<float>(
          glm::mat4(1.0f), glm::vec3(scale.x(), scale.y(), scale.z()));
      glm::mat4 Model = T * RX * RY * RZ * S;

      // Process triangles with UV coordinates
      for (size_t j = 0; j + 2 < pmesh.Vertices.size(); j += 3) {
        // Transform vertex positions
        glm::vec3 gv0(pmesh.Vertices[j].Position.X,
                      pmesh.Vertices[j].Position.Y,
                      pmesh.Vertices[j].Position.Z);
        glm::vec4 v0 = Model * glm::vec4(gv0, 1);
        glm::vec3 gv1(pmesh.Vertices[j + 1].Position.X,
                      pmesh.Vertices[j + 1].Position.Y,
                      pmesh.Vertices[j + 1].Position.Z);
        glm::vec4 v1 = Model * glm::vec4(gv1, 1);
        glm::vec3 gv2(pmesh.Vertices[j + 2].Position.X,
                      pmesh.Vertices[j + 2].Position.Y,
                      pmesh.Vertices[j + 2].Position.Z);
        glm::vec4 v2 = Model * glm::vec4(gv2, 1);

        // Get UV coordinates from OBJ
        vec3 uv0(pmesh.Vertices[j].TextureCoordinate.X,
                 pmesh.Vertices[j].TextureCoordinate.Y, 0);
        vec3 uv1(pmesh.Vertices[j + 1].TextureCoordinate.X,
                 pmesh.Vertices[j + 1].TextureCoordinate.Y, 0);
        vec3 uv2(pmesh.Vertices[j + 2].TextureCoordinate.X,
                 pmesh.Vertices[j + 2].TextureCoordinate.Y, 0);

        // Create triangle with UVs
        triangleList.push_back(triangle(
            vec3(v0.x, v0.y, v0.z), vec3(v1.x, v1.y, v1.z),
            vec3(v2.x, v2.y, v2.z), uv0, uv1, uv2, this->material_ptr));
      }
    }

    if (!g_quiet.load() && !g_suppress_mesh_messages.load())
      cerr << "Loaded " << triangleList.size() << " triangles with UV coords"
           << endl;

    return true;
  }
}

bool mesh::hit(const ray &r, double t_min, double t_max,
               hit_record &rec) const {

  for (size_t i = 0; i < triangleList.size(); i++) {
    if (triangleList[i].hit(r, t_min, t_max, rec)) {
      return true;
    }
  }
  return false;
}

bool mesh::bounding_box(aabb &output_box) const {
  if (triangleList.empty()) {
    return false;
  }

  // Use cached box if already computed
  if (box_computed) {
    output_box = cached_box;
    return true;
  }

  // Compute bounding box that encompasses all triangles
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
