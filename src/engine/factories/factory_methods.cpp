#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <system_error>
#include <vector>

#include "../../defs.h"
#include "../../util/logging.h"
#include "../camera.h"
#include "../config.h"
#include "../dielectric.h"
#include "../emissive.h"
#include "../gltf_loader.h"
#include "../hdri_environment.h"
#include "../lambertian.h"
#include "../material.h"
#include "../mesh.h"
#include "../metal.h"
#include "../pbr_material.h"
#include "../point_light.h"
#include "../sphere.h"
#include "../sss_material.h"
#include "../sun.h"
#include "../triangle.h"
#include "../world.h"
#include "factory_methods.h"

using namespace tinyxml2;

using namespace std;

vector<string> g_attempted_meshes;
vector<string> g_loaded_meshes;
vector<MeshLoadInfo> g_mesh_stats;
string g_scene_directory;

// Global material map for dynamic material lookup from XML
map<string, shared_ptr<material>> g_materials;

shared_ptr<config> LoadConfig(XMLElement *configElem);
shared_ptr<camera> LoadCamera(XMLElement *cameraElem, float aspect_ratio);
shared_ptr<sun> LoadSun(XMLElement *lightsElem);
void LoadMaterials(XMLElement *materialsElem);
vector<shared_ptr<hittable>> LoadObjects(XMLElement *objectsElem);
shared_ptr<hittable> LoadSphere(XMLElement *sphereElem);
shared_ptr<hittable> LoadMesh(XMLElement *meshElem);
shared_ptr<hittable> LoadTriangle(XMLElement *triangleElem);
shared_ptr<material> LoadMaterial(string name);

namespace {
std::filesystem::path LocateAssetsDirectory() {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path current = fs::current_path(ec);
  if (ec) {
    cerr << "LocateAssetsDirectory: failed to get current path: "
         << ec.message() << endl;
    return {};
  }

  const fs::path marker = "assets";
  fs::path probe = current;
  while (!probe.empty()) {
    fs::path candidate = probe / marker;
    if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
      return candidate;
    }

    fs::path parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = std::move(parent);
  }

  cerr << "LocateAssetsDirectory: unable to locate assets directory starting "
          "from "
       << current << endl;
  return {};
}

const std::filesystem::path &AssetsDirectory() {
  static const std::filesystem::path dir = LocateAssetsDirectory();
  return dir;
}
} // namespace

shared_ptr<world> LoadScene(string fileName) {
  XMLDocument doc;
  XMLError xmlErr = doc.LoadFile(fileName.c_str());
  if (xmlErr != XML_SUCCESS) {
    cout << "Error parsing " << fileName << " -- error code: " << xmlErr;
    if (xmlErr == XML_ERROR_MISMATCHED_ELEMENT) {
      cout << " -- Mismatched Element " << endl;
    }
    return shared_ptr<world>();
  }

  shared_ptr<world> pworld = make_shared<world>();

  // Record scene directory for resolving relative mesh filenames
  try {
    std::filesystem::path scenePath(fileName);
    if (scenePath.has_parent_path())
      g_scene_directory = scenePath.parent_path().string();
    else
      g_scene_directory.clear();
  } catch (...) {
    g_scene_directory.clear();
  }

  XMLNode *itemContainer = doc.FirstChildElement();
  if (!itemContainer) {
    cerr << "LoadScene: missing root element in " << fileName << endl;
    return shared_ptr<world>();
  }

  XMLElement *configElem = itemContainer->FirstChildElement("Config");
  // Record the attempt for diagnostics
  g_attempted_meshes.push_back(fileName);
  if (!configElem) {
    cerr << "LoadScene: missing <Config> element in " << fileName << endl;
    return shared_ptr<world>();
  }

  pworld->pconfig = LoadConfig(configElem);
  if (!pworld->pconfig) {
    g_loaded_meshes.push_back(fileName);
    cerr << "LoadScene: failed to load <Config> from " << fileName << endl;
    return shared_ptr<world>();
  }

  XMLElement *cameraElem = configElem->NextSiblingElement("Camera");
  if (!cameraElem) {
    cerr << "LoadScene: missing <Camera> element in " << fileName << endl;
    return shared_ptr<world>();
  }
  pworld->pcamera = LoadCamera(cameraElem, pworld->pconfig->ASPECT_RATIO);
  if (!pworld->pcamera) {
    cerr << "LoadScene: failed to load <Camera> from " << fileName << endl;
    return shared_ptr<world>();
  }

  XMLElement *lightsElem = configElem->NextSiblingElement("Lights");
  if (!lightsElem) {
    cerr << "LoadScene: missing <Lights> element in " << fileName << endl;
    return shared_ptr<world>();
  }
  pworld->psun = LoadSun(lightsElem);
  if (!pworld->psun) {
    cerr << "LoadScene: failed to load <Lights> from " << fileName << endl;
    return shared_ptr<world>();
  }

  // Load point lights (optional)
  for (XMLElement *pointLightElem = lightsElem->FirstChildElement("PointLight");
       pointLightElem != nullptr;
       pointLightElem = pointLightElem->NextSiblingElement("PointLight")) {
    point3 pos(0, 0, 0);
    color col(1, 1, 1);
    double intensity = 1.0;

    XMLElement *posElem = pointLightElem->FirstChildElement("Position");
    if (posElem) {
      pos = point3(posElem->Attribute("x") ? atof(posElem->Attribute("x")) : 0,
                   posElem->Attribute("y") ? atof(posElem->Attribute("y")) : 0,
                   posElem->Attribute("z") ? atof(posElem->Attribute("z")) : 0);
    }

    XMLElement *colElem = pointLightElem->FirstChildElement("Color");
    if (colElem) {
      col = color(colElem->Attribute("r") ? atof(colElem->Attribute("r")) : 1,
                  colElem->Attribute("g") ? atof(colElem->Attribute("g")) : 1,
                  colElem->Attribute("b") ? atof(colElem->Attribute("b")) : 1);
    }

    XMLElement *intensElem = pointLightElem->FirstChildElement("Intensity");
    if (intensElem && intensElem->Attribute("value")) {
      intensity = atof(intensElem->Attribute("value"));
    }

    pworld->pointLights.push_back(make_shared<PointLight>(pos, col, intensity));
  }

  // Load materials from XML (optional, for dynamic material definitions)
  XMLElement *materialsElem = configElem->NextSiblingElement("Materials");
  if (materialsElem) {
    LoadMaterials(materialsElem);
  }

  XMLElement *objectsElem = configElem->NextSiblingElement("Objects");
  if (!objectsElem) {
    cerr << "LoadScene: missing <Objects> element in " << fileName << endl;
    return shared_ptr<world>();
  }
  pworld->objects = LoadObjects(objectsElem);

  // Load HDRI environment map (optional)
  XMLElement *envElem = configElem->NextSiblingElement("Environment");
  if (envElem) {
    const char *hdriPath = envElem->Attribute("hdri");
    if (hdriPath) {
      pworld->hdri = make_shared<hdri_environment>();
      // Try loading relative to scene directory first
      std::string fullPath =
          g_scene_directory.empty()
              ? hdriPath
              : (std::filesystem::path(g_scene_directory) / hdriPath).string();
      if (!pworld->hdri->load(fullPath)) {
        // Try from assets directory
        auto assetsDir = AssetsDirectory();
        if (!assetsDir.empty()) {
          pworld->hdri->load((assetsDir / hdriPath).string());
        }
      }
      // Optional intensity setting
      if (envElem->Attribute("intensity")) {
        pworld->hdri->intensity = atof(envElem->Attribute("intensity"));
      }
      // Optional rotation setting
      if (envElem->Attribute("rotation")) {
        pworld->hdri->rotation =
            atof(envElem->Attribute("rotation")) * M_PI / 180.0;
      }
    }
  }

  return pworld;
}

shared_ptr<config> LoadConfig(XMLElement *configElem) {

  if (!configElem) {
    cerr << "LoadConfig: null config element" << endl;
    return shared_ptr<config>();
  }

  XMLElement *widthElem = configElem->FirstChildElement("Width");
  if (!widthElem || !widthElem->Attribute("value")) {
    cerr << "LoadConfig: missing <Width value=...>" << endl;
    return shared_ptr<config>();
  }
  int width = atoi(widthElem->Attribute("value"));

  XMLElement *aspectElem = widthElem->NextSiblingElement("Aspect_ratio");
  if (!aspectElem || !aspectElem->Attribute("value")) {
    cerr << "LoadConfig: missing <Aspect_ratio value=...>" << endl;
    return shared_ptr<config>();
  }
  float aspect_ratio = atof(aspectElem->Attribute("value"));

  XMLElement *samplesElem = widthElem->NextSiblingElement("Samples_Per_Pixel");
  if (!samplesElem || !samplesElem->Attribute("value")) {
    cerr << "LoadConfig: missing <Samples_Per_Pixel value=...>" << endl;
    return shared_ptr<config>();
  }
  int samples = atoi(samplesElem->Attribute("value"));

  XMLElement *depthElem = widthElem->NextSiblingElement("Max_Depth");
  if (!depthElem || !depthElem->Attribute("value")) {
    cerr << "LoadConfig: missing <Max_Depth value=...>" << endl;
    return shared_ptr<config>();
  }
  int depth = atoi(depthElem->Attribute("value"));

  shared_ptr<config> pconfig = make_shared<config>();
  pconfig->ASPECT_RATIO = aspect_ratio;
  pconfig->IMAGE_WIDTH = width;
  pconfig->IMAGE_HEIGHT = static_cast<int>(width / aspect_ratio);
  pconfig->SAMPLES_PER_PIXEL = samples;
  pconfig->MAX_DEPTH = depth;

  return pconfig;
}

shared_ptr<camera> LoadCamera(XMLElement *cameraElem, float aspect_ratio) {
  if (!cameraElem) {
    cerr << "LoadCamera: null camera element" << endl;
    return shared_ptr<camera>();
  }

  // Viewport width (optional, default to 2.0)
  float vpWidth = 2.0f;
  XMLElement *vpWidthElem = cameraElem->FirstChildElement("Viewport_Width");
  if (vpWidthElem && vpWidthElem->Attribute("value")) {
    vpWidth = atof(vpWidthElem->Attribute("value"));
  }

  // Focal length (optional, default to 1.0)
  float focal_length = 1.0f;
  XMLElement *focalElem = cameraElem->FirstChildElement("Focal_Length");
  if (focalElem && focalElem->Attribute("value")) {
    focal_length = atof(focalElem->Attribute("value"));
  }

  // Look from (required)
  float x = 0.0f, y = 0.0f, z = 1.0f;
  XMLElement *lookFromElem = cameraElem->FirstChildElement("Look_From");
  if (lookFromElem) {
    if (lookFromElem->Attribute("x"))
      x = atof(lookFromElem->Attribute("x"));
    if (lookFromElem->Attribute("y"))
      y = atof(lookFromElem->Attribute("y"));
    if (lookFromElem->Attribute("z"))
      z = atof(lookFromElem->Attribute("z"));
  }
  vec3 lookFrom(x, y, z);

  // Look at (required)
  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
  XMLElement *lookAtElem = cameraElem->FirstChildElement("Look_at");
  if (lookAtElem) {
    if (lookAtElem->Attribute("x"))
      x = atof(lookAtElem->Attribute("x"));
    if (lookAtElem->Attribute("y"))
      y = atof(lookAtElem->Attribute("y"));
    if (lookAtElem->Attribute("z"))
      z = atof(lookAtElem->Attribute("z"));
  }
  vec3 lookAt(x, y, z);

  // Up vector (optional, default to (0,1,0))
  x = 0.0f;
  y = 1.0f;
  z = 0.0f;
  XMLElement *upElem = cameraElem->FirstChildElement("Up");
  if (upElem) {
    if (upElem->Attribute("x"))
      x = atof(upElem->Attribute("x"));
    if (upElem->Attribute("y"))
      y = atof(upElem->Attribute("y"));
    if (upElem->Attribute("z"))
      z = atof(upElem->Attribute("z"));
  }
  vec3 up(x, y, z);

  // FOV (optional, default to 90)
  float fov = 90.0f;
  XMLElement *fovElem = cameraElem->FirstChildElement("FOV");
  if (fovElem && fovElem->Attribute("angle")) {
    fov = atof(fovElem->Attribute("angle"));
  }

  shared_ptr<camera> pcamera =
      make_shared<camera>(lookFrom, lookAt, up, fov, aspect_ratio);
  pcamera->VIEWPORT_WIDTH = vpWidth;
  pcamera->VIEWPORT_HEIGHT = vpWidth / aspect_ratio;
  pcamera->ASPECT_RATIO = aspect_ratio;
  pcamera->FOCAL_LENGTH = focal_length;

  return pcamera;
}

shared_ptr<sun> LoadSun(XMLElement *lightsElem) {
  if (!lightsElem) {
    cerr << "LoadSun: null lights element" << endl;
    return shared_ptr<sun>();
  }

  XMLElement *sunElem = lightsElem->FirstChildElement("Sun");
  if (!sunElem) {
    cerr << "LoadSun: missing <Sun> element" << endl;
    return shared_ptr<sun>();
  }

  XMLElement *dirElem = sunElem->FirstChildElement("Direction");
  float x = 0.0f, y = 1.0f, z = 0.0f;
  if (dirElem) {
    if (dirElem->Attribute("x"))
      x = atof(dirElem->Attribute("x"));
    if (dirElem->Attribute("y"))
      y = atof(dirElem->Attribute("y"));
    if (dirElem->Attribute("z"))
      z = atof(dirElem->Attribute("z"));
  }
  vec3 dir(x, y, z);

  float intensity = 1.0f;
  XMLElement *intensityElem = sunElem->FirstChildElement("Intensity");
  if (intensityElem && intensityElem->Attribute("value")) {
    intensity = atof(intensityElem->Attribute("value"));
  }

  float r = 1.0f, g = 1.0f, b = 1.0f;
  XMLElement *colorElem = sunElem->FirstChildElement("Color");
  if (colorElem) {
    if (colorElem->Attribute("r"))
      r = atof(colorElem->Attribute("r"));
    if (colorElem->Attribute("g"))
      g = atof(colorElem->Attribute("g"));
    if (colorElem->Attribute("b"))
      b = atof(colorElem->Attribute("b"));
  }
  vec3 color(r, g, b);

  shared_ptr<sun> psun = make_shared<sun>();
  psun->direction = dir;
  psun->sunColor = color;

  return psun;
}

vector<shared_ptr<hittable>> LoadObjects(XMLElement *objectsElem) {

  vector<shared_ptr<hittable>> list;

  XMLElement *item = objectsElem->FirstChildElement();
  while (item) {
    string type = item->Name();
    if (type == "Sphere") {
      auto obj = LoadSphere(item);
      if (obj)
        list.push_back(obj);
    } else if (type == "Mesh") {
      auto obj = LoadMesh(item);
      if (obj)
        list.push_back(obj);
    } else if (type == "Triangle") {
      auto obj = LoadTriangle(item);
      if (obj)
        list.push_back(obj);
    } else if (type == "GLTF") {
      // Load glTF 2.0 model
      const char *file = item->Attribute("file");
      if (file) {
        float x = 0, y = 0, z = 0;
        float sx = 1, sy = 1, sz = 1;
        float rx = 0, ry = 0, rz = 0;

        XMLElement *posElem = item->FirstChildElement("Position");
        if (posElem) {
          if (posElem->Attribute("x"))
            x = atof(posElem->Attribute("x"));
          if (posElem->Attribute("y"))
            y = atof(posElem->Attribute("y"));
          if (posElem->Attribute("z"))
            z = atof(posElem->Attribute("z"));
        }
        XMLElement *scaleElem = item->FirstChildElement("Scale");
        if (scaleElem) {
          if (scaleElem->Attribute("x"))
            sx = atof(scaleElem->Attribute("x"));
          if (scaleElem->Attribute("y"))
            sy = atof(scaleElem->Attribute("y"));
          if (scaleElem->Attribute("z"))
            sz = atof(scaleElem->Attribute("z"));
        }
        XMLElement *rotElem = item->FirstChildElement("Rotation");
        if (rotElem) {
          if (rotElem->Attribute("x"))
            rx = atof(rotElem->Attribute("x"));
          if (rotElem->Attribute("y"))
            ry = atof(rotElem->Attribute("y"));
          if (rotElem->Attribute("z"))
            rz = atof(rotElem->Attribute("z"));
        }

        // Resolve path
        std::string gltfPath = file;
        if (!g_scene_directory.empty()) {
          gltfPath = (std::filesystem::path(g_scene_directory) / file).string();
        }

        auto result = gltf_loader::load(gltfPath, vec3(x, y, z),
                                        vec3(sx, sy, sz), vec3(rx, ry, rz));
        if (result.success) {
          for (auto &obj : result.objects) {
            list.push_back(obj);
          }
        }
      }
    }

    item = item->NextSiblingElement();
  }

  return list;
}

shared_ptr<hittable> LoadSphere(XMLElement *sphereElem) {
  if (!sphereElem) {
    cerr << "LoadSphere: null sphere element" << endl;
    return shared_ptr<hittable>();
  }

  string name = sphereElem->Name();

  // Radius (required)
  double radius = 0.5;
  XMLElement *radiusElem = sphereElem->FirstChildElement("Radius");
  if (radiusElem && radiusElem->Attribute("value")) {
    radius = atof(radiusElem->Attribute("value"));
  }

  // Position (required)
  float x = 0.0f, y = 0.0f, z = 0.0f;
  XMLElement *posElem = sphereElem->FirstChildElement("Position");
  if (posElem) {
    if (posElem->Attribute("x"))
      x = atof(posElem->Attribute("x"));
    if (posElem->Attribute("y"))
      y = atof(posElem->Attribute("y"));
    if (posElem->Attribute("z"))
      z = atof(posElem->Attribute("z"));
  }
  vec3 center(x, y, z);

  // Scale (optional, default 1,1,1)
  x = 1.0f;
  y = 1.0f;
  z = 1.0f;
  XMLElement *scaleElem = sphereElem->FirstChildElement("Scale");
  if (scaleElem) {
    if (scaleElem->Attribute("x"))
      x = atof(scaleElem->Attribute("x"));
    if (scaleElem->Attribute("y"))
      y = atof(scaleElem->Attribute("y"));
    if (scaleElem->Attribute("z"))
      z = atof(scaleElem->Attribute("z"));
  }
  vec3 scale(x, y, z);

  // Rotation (optional, default 0,0,0)
  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
  XMLElement *rotateElem = sphereElem->FirstChildElement("Rotation");
  if (rotateElem) {
    if (rotateElem->Attribute("x"))
      x = atof(rotateElem->Attribute("x"));
    if (rotateElem->Attribute("y"))
      y = atof(rotateElem->Attribute("y"));
    if (rotateElem->Attribute("z"))
      z = atof(rotateElem->Attribute("z"));
  }
  vec3 rotation(x, y, z);

  // Material
  string materialName;
  XMLElement *materialElem = sphereElem->FirstChildElement("Material");
  if (materialElem && materialElem->Attribute("name")) {
    materialName = materialElem->Attribute("name");
  }

  auto material = LoadMaterial(materialName);
  shared_ptr<hittable> psphere = make_shared<sphere>(center, radius, material);

  return psphere;
}

shared_ptr<hittable> LoadTriangle(XMLElement *triangleElem) {
  if (!triangleElem) {
    cerr << "LoadTriangle: null triangle element" << endl;
    return shared_ptr<hittable>();
  }

  string name = triangleElem->Name();

  float x = 0.0f, y = 0.0f, z = 0.0f;
  XMLElement *v0Elem = triangleElem->FirstChildElement("V0");
  if (!v0Elem) {
    cerr << "LoadTriangle: missing V0 element" << endl;
    return shared_ptr<hittable>();
  }
  if (v0Elem->Attribute("x"))
    x = atof(v0Elem->Attribute("x"));
  if (v0Elem->Attribute("y"))
    y = atof(v0Elem->Attribute("y"));
  if (v0Elem->Attribute("z"))
    z = atof(v0Elem->Attribute("z"));
  point3 v0(x, y, z);

  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
  XMLElement *v1Elem = triangleElem->FirstChildElement("V1");
  if (!v1Elem) {
    cerr << "LoadTriangle: missing V1 element" << endl;
    return shared_ptr<hittable>();
  }
  if (v1Elem->Attribute("x"))
    x = atof(v1Elem->Attribute("x"));
  if (v1Elem->Attribute("y"))
    y = atof(v1Elem->Attribute("y"));
  if (v1Elem->Attribute("z"))
    z = atof(v1Elem->Attribute("z"));
  point3 v1(x, y, z);

  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
  XMLElement *v2Elem = triangleElem->FirstChildElement("V2");
  if (!v2Elem) {
    cerr << "LoadTriangle: missing V2 element" << endl;
    return shared_ptr<hittable>();
  }
  if (v2Elem->Attribute("x"))
    x = atof(v2Elem->Attribute("x"));
  if (v2Elem->Attribute("y"))
    y = atof(v2Elem->Attribute("y"));
  if (v2Elem->Attribute("z"))
    z = atof(v2Elem->Attribute("z"));
  point3 v2(x, y, z);

  string materialName;
  XMLElement *materialElem = triangleElem->FirstChildElement("Material");
  if (materialElem && materialElem->Attribute("name")) {
    materialName = materialElem->Attribute("name");
  }
  auto material = LoadMaterial(materialName);

  shared_ptr<hittable> ptriangle = make_shared<triangle>(
      vec3(v0.x(), v0.y(), v0.z()), vec3(v1.x(), v1.y(), v1.z()),
      vec3(v2.x(), v2.y(), v2.z()), material);
  return ptriangle;
}

shared_ptr<hittable> LoadMesh(XMLElement *meshElem) {
  if (!meshElem) {
    cerr << "LoadMesh: null mesh element" << endl;
    return shared_ptr<hittable>();
  }

  string name = meshElem->Name();

  // Position (optional, default 0,0,0)
  float x = 0.0f, y = 0.0f, z = 0.0f;
  XMLElement *posElem = meshElem->FirstChildElement("Position");
  if (posElem) {
    if (posElem->Attribute("x"))
      x = atof(posElem->Attribute("x"));
    if (posElem->Attribute("y"))
      y = atof(posElem->Attribute("y"));
    if (posElem->Attribute("z"))
      z = atof(posElem->Attribute("z"));
  }
  vec3 position(x, y, z);

  // Scale (optional, default 1,1,1)
  x = 1.0f;
  y = 1.0f;
  z = 1.0f;
  XMLElement *scaleElem = meshElem->FirstChildElement("Scale");
  if (scaleElem) {
    if (scaleElem->Attribute("x"))
      x = atof(scaleElem->Attribute("x"));
    if (scaleElem->Attribute("y"))
      y = atof(scaleElem->Attribute("y"));
    if (scaleElem->Attribute("z"))
      z = atof(scaleElem->Attribute("z"));
  }
  vec3 scale(x, y, z);

  // Rotation (optional, default 0,0,0)
  x = 0.0f;
  y = 0.0f;
  z = 0.0f;
  XMLElement *rotateElem = meshElem->FirstChildElement("Rotation");
  if (rotateElem) {
    if (rotateElem->Attribute("x"))
      x = atof(rotateElem->Attribute("x"));
    if (rotateElem->Attribute("y"))
      y = atof(rotateElem->Attribute("y"));
    if (rotateElem->Attribute("z"))
      z = atof(rotateElem->Attribute("z"));
  }
  vec3 rotation(x, y, z);

  // Material
  string materialName;
  XMLElement *materialElem = meshElem->FirstChildElement("Material");
  if (materialElem && materialElem->Attribute("name")) {
    materialName = materialElem->Attribute("name");
  }
  auto material = LoadMaterial(materialName);

  // File
  string fileName;
  XMLElement *fileElem = meshElem->FirstChildElement("File");
  if (fileElem && fileElem->Attribute("name")) {
    fileName = fileElem->Attribute("name");
  }
  if (fileName.empty()) {
    cerr << "LoadMesh: missing File element or name attribute" << endl;
    return shared_ptr<hittable>();
  }
  namespace fs = std::filesystem;
  const fs::path &assetsRoot = AssetsDirectory();

  // We'll try a few candidate paths in order. Record each attempt for
  // diagnostics.
  std::vector<std::string> candidates;
  auto addCandidateString = [&candidates](const std::string &value) {
    if (value.empty()) {
      return;
    }
    if (std::find(candidates.begin(), candidates.end(), value) ==
        candidates.end()) {
      candidates.push_back(value);
    }
  };

  auto addCandidatePath = [&addCandidateString](const fs::path &path) {
    if (path.empty()) {
      return;
    }
    addCandidateString(path.string());
  };

  // Priority: assets/ directory first, then scene directory, then as provided
  if (!assetsRoot.empty()) {
    addCandidatePath(assetsRoot / fileName);
  }
  if (!g_scene_directory.empty()) {
    addCandidatePath(fs::path(g_scene_directory) / fileName);
  }
  addCandidateString(fileName); // as provided (may be absolute or relative)

  shared_ptr<hittable> pmesh =
      make_shared<mesh>(fileName, position, scale, rotation, material);
  shared_ptr<mesh> derived = dynamic_pointer_cast<mesh>(pmesh);

  // When trying multiple candidates, suppress mesh's own per-file prints so
  // we only show final results from the scene loader.
  struct MeshMessageGuard {
    MeshMessageGuard() {
      previous = g_suppress_mesh_messages.load();
      g_suppress_mesh_messages = true;
    }
    ~MeshMessageGuard() { g_suppress_mesh_messages = previous; }
    bool previous{false};
  } guard;

  for (auto &candidate : candidates) {
    // record attempt
    g_attempted_meshes.push_back(candidate);
    auto t0 = std::chrono::high_resolution_clock::now();
    if (derived->load(candidate)) {
      auto t1 = std::chrono::high_resolution_clock::now();
      double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
      g_loaded_meshes.push_back(candidate);
      MeshLoadInfo info;
      info.name = candidate;
      info.load_ms = ms;
      info.triangles = derived->getTriangleCount();
      g_mesh_stats.push_back(info);
      return pmesh;
    }
  }

  // If the simple candidates failed, attempt a recursive search in a few
  // likely directories for a matching basename (this helps when the .obj
  // lives somewhere else in the tree). Stop after first match.
  std::string basename = fs::path(fileName).filename().string();
  std::vector<fs::path> search_roots;
  auto addSearchRoot = [&search_roots](const fs::path &root) {
    if (root.empty()) {
      return;
    }
    if (std::find(search_roots.begin(), search_roots.end(), root) ==
        search_roots.end()) {
      search_roots.push_back(root);
    }
  };

  // Search assets/ first, then scene directory, then cwd
  if (!assetsRoot.empty()) {
    addSearchRoot(assetsRoot);
  }
  if (!g_scene_directory.empty())
    addSearchRoot(g_scene_directory);
  addSearchRoot(fs::current_path());

  for (auto &root : search_roots) {
    try {
      if (!fs::exists(root))
        continue;
      int seen = 0;
      for (auto &ent : fs::recursive_directory_iterator(root)) {
        if (!ent.is_regular_file())
          continue;
        seen++;
        if (seen > 5000)
          break; // don't scan forever in very large trees
        if (ent.path().filename() == basename) {
          std::string found = ent.path().string();
          g_attempted_meshes.push_back(found);
          auto t0 = std::chrono::high_resolution_clock::now();
          if (derived->load(found)) {
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            g_loaded_meshes.push_back(found);
            MeshLoadInfo info;
            info.name = found;
            info.load_ms = ms;
            info.triangles = derived->getTriangleCount();
            g_mesh_stats.push_back(info);
            return pmesh;
          }
          // if failed, continue searching
        }
      }
    } catch (...) {
    }
  }

  cerr << "LoadMesh: failed to load mesh file '" << fileName << "' (tried ";
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (i)
      cerr << ", ";
    cerr << candidates[i];
  }
  cerr << ") and searched common directories" << endl;

  return shared_ptr<hittable>();
}

// Parse materials from XML <Materials> section
void LoadMaterials(XMLElement *materialsElem) {
  if (!materialsElem) {
    cerr << "LoadMaterials: null materialsElem" << endl;
    return;
  }

  cerr << "LoadMaterials: parsing materials from XML..." << endl;

  // Clear previous materials
  g_materials.clear();

  XMLElement *item = materialsElem->FirstChildElement();
  while (item) {
    string type = item->Name();
    const char *nameAttr = item->Attribute("name");
    if (!nameAttr) {
      item = item->NextSiblingElement();
      continue;
    }
    string name = nameAttr;

    // Parse color
    float r = 0.5f, g = 0.5f, b = 0.5f;
    XMLElement *colorElem = item->FirstChildElement("Color");
    if (colorElem) {
      if (colorElem->Attribute("r"))
        r = atof(colorElem->Attribute("r"));
      if (colorElem->Attribute("g"))
        g = atof(colorElem->Attribute("g"));
      if (colorElem->Attribute("b"))
        b = atof(colorElem->Attribute("b"));
    }

    shared_ptr<material> mat;
    if (type == "Lambertian") {
      mat = make_shared<lambertian>(color(r, g, b));
    } else if (type == "Metal") {
      float fuzz = 0.0f;
      XMLElement *fuzzElem = item->FirstChildElement("Fuzz");
      if (fuzzElem && fuzzElem->Attribute("value")) {
        fuzz = atof(fuzzElem->Attribute("value"));
      }
      mat = make_shared<metal>(color(r, g, b), fuzz);
    } else if (type == "Emissive") {
      float strength = 1.0f;
      XMLElement *strengthElem = item->FirstChildElement("Strength");
      if (strengthElem && strengthElem->Attribute("value")) {
        strength = atof(strengthElem->Attribute("value"));
      }
      // Emissive uses color * strength
      mat = make_shared<emissive>(
          color(r * strength, g * strength, b * strength));
    } else if (type == "Dielectric") {
      float ior = 1.5f; // Default glass
      XMLElement *iorElem = item->FirstChildElement("IOR");
      if (iorElem && iorElem->Attribute("value")) {
        ior = atof(iorElem->Attribute("value"));
      }
      // Create glass with slight blue-white tint for realistic appearance
      color glassTint(0.95, 0.97, 1.0); // Slight blue-white tint
      mat = make_shared<dielectric>(ior, glassTint);
    } else if (type == "PBR") {
      // PBR material with metallic/roughness workflow
      float metallic = 0.0f, roughness = 0.5f;
      XMLElement *metallicElem = item->FirstChildElement("Metallic");
      if (metallicElem && metallicElem->Attribute("value")) {
        metallic = atof(metallicElem->Attribute("value"));
      }
      XMLElement *roughnessElem = item->FirstChildElement("Roughness");
      if (roughnessElem && roughnessElem->Attribute("value")) {
        roughness = atof(roughnessElem->Attribute("value"));
      }
      mat = make_shared<pbr_material>(color(r, g, b), metallic, roughness);
    } else if (type == "SSS") {
      // Subsurface scattering material (marble, skin, wax)
      float scatter_dist = 0.5f;
      color scatter_col(1.0f, 0.8f, 0.6f); // Default warm scatter
      XMLElement *scatterElem = item->FirstChildElement("ScatterDistance");
      if (scatterElem && scatterElem->Attribute("value")) {
        scatter_dist = atof(scatterElem->Attribute("value"));
      }
      XMLElement *scatterColElem = item->FirstChildElement("ScatterColor");
      if (scatterColElem) {
        if (scatterColElem->Attribute("r"))
          scatter_col.e[0] = atof(scatterColElem->Attribute("r"));
        if (scatterColElem->Attribute("g"))
          scatter_col.e[1] = atof(scatterColElem->Attribute("g"));
        if (scatterColElem->Attribute("b"))
          scatter_col.e[2] = atof(scatterColElem->Attribute("b"));
      }
      mat =
          make_shared<sss_material>(color(r, g, b), scatter_col, scatter_dist);
    }

    if (mat) {
      g_materials[name] = mat;
    }

    item = item->NextSiblingElement();
  }
}

shared_ptr<material> LoadMaterial(string name) {
  // First check dynamically loaded materials from XML
  auto it = g_materials.find(name);
  if (it != g_materials.end()) {
    cerr << "LoadMaterial: found dynamic material '" << name << "'" << endl;
    return it->second;
  }

  cerr << "LoadMaterial: using fallback for '" << name
       << "' (not in XML materials)" << endl;

  // Fallback to hardcoded presets for backward compatibility
  shared_ptr<material> pmaterial;

  if (name == "ground") {
    pmaterial = make_shared<lambertian>(color(0.8, 0.8, 0.0));
  } else if (name == "mattBrown") {
    pmaterial = make_shared<lambertian>(color(0.7, 0.3, 0.3));
  } else if (name == "fuzzySilver") {
    pmaterial = make_shared<metal>(color(0.8, 0.8, 0.8), 0.3);
  } else if (name == "shinyGold") {
    pmaterial = make_shared<metal>(color(0.8, 0.6, 0.2), 1.0);
  } else if (name == "emissive") {
    pmaterial = make_shared<emissive>(color(1.0, 1.0, 1.0));
  } else {
    // Default material: gray lambertian to prevent null pointer crashes
    pmaterial = make_shared<lambertian>(color(0.5, 0.5, 0.5));
  }

  return pmaterial;
}