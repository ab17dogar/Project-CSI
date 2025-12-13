#include "XMLSceneSerializer.h"
#include <QFile>
#include <QTextStream>

namespace Raytracer {
namespace scene {

// ===== Public Interface =====

SerializationResult XMLSceneSerializer::load(const QString &filePath,
                                             SceneDocument *document) {
  if (!document) {
    return SerializationResult::error(QStringLiteral("Null document pointer"));
  }

  tinyxml2::XMLDocument doc;
  tinyxml2::XMLError err = doc.LoadFile(filePath.toStdString().c_str());

  if (err != tinyxml2::XML_SUCCESS) {
    return SerializationResult::error(
        QStringLiteral("XML parse error: %1").arg(doc.ErrorStr()),
        doc.ErrorLineNum());
  }

  return loadFromXMLDocument(doc, document);
}

SerializationResult XMLSceneSerializer::save(const QString &filePath,
                                             const SceneDocument *document) {
  if (!document) {
    return SerializationResult::error(QStringLiteral("Null document pointer"));
  }

  tinyxml2::XMLDocument doc;

  // XML declaration
  tinyxml2::XMLDeclaration *decl = doc.NewDeclaration("xml version='1.0'");
  doc.InsertFirstChild(decl);

  // Root element
  tinyxml2::XMLElement *root = doc.NewElement("ItemContainer");
  doc.InsertEndChild(root);

  // Save all sections
  saveConfig(doc, root, document);
  saveCamera(doc, root, document);
  saveLights(doc, root, document);
  saveMaterials(doc, root, document);
  saveObjects(doc, root, document);

  // Write to file
  tinyxml2::XMLError err = doc.SaveFile(filePath.toStdString().c_str());
  if (err != tinyxml2::XML_SUCCESS) {
    return SerializationResult::error(
        QStringLiteral("Failed to save file: %1").arg(doc.ErrorStr()));
  }

  return SerializationResult::ok();
}

SerializationResult
XMLSceneSerializer::loadFromString(const QString &content,
                                   SceneDocument *document) {
  if (!document) {
    return SerializationResult::error(QStringLiteral("Null document pointer"));
  }

  tinyxml2::XMLDocument doc;
  tinyxml2::XMLError err = doc.Parse(content.toStdString().c_str());

  if (err != tinyxml2::XML_SUCCESS) {
    return SerializationResult::error(
        QStringLiteral("XML parse error: %1").arg(doc.ErrorStr()),
        doc.ErrorLineNum());
  }

  return loadFromXMLDocument(doc, document);
}

SerializationResult
XMLSceneSerializer::saveToString(const SceneDocument *document,
                                 QString &output) {
  if (!document) {
    return SerializationResult::error(QStringLiteral("Null document pointer"));
  }

  tinyxml2::XMLDocument doc;

  // XML declaration
  tinyxml2::XMLDeclaration *decl = doc.NewDeclaration("xml version='1.0'");
  doc.InsertFirstChild(decl);

  // Root element
  tinyxml2::XMLElement *root = doc.NewElement("ItemContainer");
  doc.InsertEndChild(root);

  // Save all sections
  saveConfig(doc, root, document);
  saveCamera(doc, root, document);
  saveLights(doc, root, document);
  saveMaterials(doc, root, document);
  saveObjects(doc, root, document);

  // Convert to string
  tinyxml2::XMLPrinter printer;
  doc.Print(&printer);
  output = QString::fromStdString(printer.CStr());

  return SerializationResult::ok();
}

// ===== Loading Helpers =====

SerializationResult
XMLSceneSerializer::loadFromXMLDocument(tinyxml2::XMLDocument &doc,
                                        SceneDocument *document) {
  document->clear();

  tinyxml2::XMLElement *root = doc.FirstChildElement("ItemContainer");
  if (!root) {
    root = doc.FirstChildElement(); // Try any root element
    if (!root) {
      return SerializationResult::error(QStringLiteral("Missing root element"));
    }
  }

  // Load config section
  tinyxml2::XMLElement *configElem = root->FirstChildElement("Config");
  if (configElem) {
    if (!loadConfig(configElem, document)) {
      return SerializationResult::error(
          QStringLiteral("Failed to load Config section"));
    }
  }

  // Load camera section
  tinyxml2::XMLElement *cameraElem = root->FirstChildElement("Camera");
  if (cameraElem) {
    if (!loadCamera(cameraElem, document)) {
      return SerializationResult::error(
          QStringLiteral("Failed to load Camera section"));
    }
  }

  // Load lights section
  tinyxml2::XMLElement *lightsElem = root->FirstChildElement("Lights");
  if (lightsElem) {
    if (!loadLights(lightsElem, document)) {
      return SerializationResult::error(
          QStringLiteral("Failed to load Lights section"));
    }
  }

  // Load materials section
  tinyxml2::XMLElement *materialsElem = root->FirstChildElement("Materials");
  if (materialsElem) {
    if (!loadMaterials(materialsElem, document)) {
      return SerializationResult::error(
          QStringLiteral("Failed to load Materials section"));
    }
  }

  // Load objects section
  tinyxml2::XMLElement *objectsElem = root->FirstChildElement("Objects");
  if (objectsElem) {
    if (!loadObjects(objectsElem, document)) {
      return SerializationResult::error(
          QStringLiteral("Failed to load Objects section"));
    }
  }

  return SerializationResult::ok();
}

bool XMLSceneSerializer::loadConfig(tinyxml2::XMLElement *configElem,
                                    SceneDocument *document) {
  RenderConfig config;

  if (auto *elem = configElem->FirstChildElement("Width")) {
    config.width = elem->IntAttribute("value", 800);
  }

  // Calculate height from aspect ratio if provided
  float aspectRatio = 16.0f / 9.0f;
  if (auto *elem = configElem->FirstChildElement("Aspect_ratio")) {
    aspectRatio = elem->FloatAttribute("value", 16.0f / 9.0f);
  }
  config.height = static_cast<int>(config.width / aspectRatio);

  if (auto *elem = configElem->FirstChildElement("Samples_Per_Pixel")) {
    config.samplesPerPixel = elem->IntAttribute("value", 50);
  }

  if (auto *elem = configElem->FirstChildElement("Max_Depth")) {
    config.maxDepth = elem->IntAttribute("value", 10);
  }

  document->setRenderConfig(config);
  return true;
}

bool XMLSceneSerializer::loadCamera(tinyxml2::XMLElement *cameraElem,
                                    SceneDocument *document) {
  CameraSettings camera;

  // Note: viewportWidth and focalLength from old format are ignored
  // as they are not part of CameraSettings anymore

  if (auto *elem = cameraElem->FirstChildElement("Look_From")) {
    camera.lookFrom = readVec3(elem);
  }

  if (auto *elem = cameraElem->FirstChildElement("Look_at")) {
    camera.lookAt = readVec3(elem);
  }

  if (auto *elem = cameraElem->FirstChildElement("Up")) {
    camera.up = readVec3(elem);
  }

  if (auto *elem = cameraElem->FirstChildElement("FOV")) {
    camera.fov = elem->FloatAttribute("angle", 90.0f);
  }

  document->setCamera(camera);
  return true;
}

bool XMLSceneSerializer::loadLights(tinyxml2::XMLElement *lightsElem,
                                    SceneDocument *document) {
  SunSettings sun;

  if (auto *sunElem = lightsElem->FirstChildElement("Sun")) {
    if (auto *elem = sunElem->FirstChildElement("Direction")) {
      sun.direction = readVec3(elem);
    }

    if (auto *elem = sunElem->FirstChildElement("Intensity")) {
      sun.intensity = elem->FloatAttribute("value", 1.0f);
    }

    if (auto *elem = sunElem->FirstChildElement("Color")) {
      sun.color = readColorRgb(elem);
    }
  }

  document->setSun(sun);
  return true;
}

bool XMLSceneSerializer::loadMaterials(tinyxml2::XMLElement *materialsElem,
                                       SceneDocument *document) {
  // Load Lambertian materials
  for (auto *elem = materialsElem->FirstChildElement("Lambertian"); elem;
       elem = elem->NextSiblingElement("Lambertian")) {
    QString name = QString::fromStdString(
        elem->Attribute("name") ? elem->Attribute("name") : "");

    // Read color first, defaulting to gray
    glm::vec3 color(0.7f, 0.7f, 0.7f);
    if (auto *colorElem = elem->FirstChildElement("Color")) {
      color = readColorRgb(colorElem);
    }

    auto material = MaterialDefinition::createLambertian(name, color);
    document->addMaterial(std::move(material));
  }

  // Load Metal materials
  for (auto *elem = materialsElem->FirstChildElement("Metal"); elem;
       elem = elem->NextSiblingElement("Metal")) {
    QString name = QString::fromStdString(
        elem->Attribute("name") ? elem->Attribute("name") : "");

    // Read color first, defaulting to silver
    glm::vec3 color(0.8f, 0.8f, 0.8f);
    if (auto *colorElem = elem->FirstChildElement("Color")) {
      color = readColorRgb(colorElem);
    }

    float fuzz = 0.0f;
    if (auto *fuzzElem = elem->FirstChildElement("Fuzz")) {
      const char *val = fuzzElem->Attribute("value");
      if (val && strlen(val) > 0) {
        fuzz = fuzzElem->FloatAttribute("value", 0.0f);
      }
    }

    auto material = MaterialDefinition::createMetal(name, color, fuzz);
    document->addMaterial(std::move(material));
  }

  // Load Emissive materials
  for (auto *elem = materialsElem->FirstChildElement("Emissive"); elem;
       elem = elem->NextSiblingElement("Emissive")) {
    QString name = QString::fromStdString(
        elem->Attribute("name") ? elem->Attribute("name") : "");

    // Read color first, defaulting to white
    glm::vec3 color(1.0f, 1.0f, 1.0f);
    if (auto *colorElem = elem->FirstChildElement("Color")) {
      color = readColorRgb(colorElem);
    }

    float strength = 1.0f;
    if (auto *strengthElem = elem->FirstChildElement("Strength")) {
      strength = strengthElem->FloatAttribute("value", 1.0f);
    }

    auto material = MaterialDefinition::createEmissive(name, color, strength);
    document->addMaterial(std::move(material));
  }

  return true;
}

bool XMLSceneSerializer::loadObjects(tinyxml2::XMLElement *objectsElem,
                                     SceneDocument *document) {
  SceneNode *root = document->rootNode();

  // Load Spheres
  for (auto *elem = objectsElem->FirstChildElement("Sphere"); elem;
       elem = elem->NextSiblingElement("Sphere")) {
    if (!loadSphere(elem, document, root)) {
      return false;
    }
  }

  // Load Triangles
  for (auto *elem = objectsElem->FirstChildElement("Triangle"); elem;
       elem = elem->NextSiblingElement("Triangle")) {
    if (!loadTriangle(elem, document, root)) {
      return false;
    }
  }

  // Load Meshes
  for (auto *elem = objectsElem->FirstChildElement("Mesh"); elem;
       elem = elem->NextSiblingElement("Mesh")) {
    if (!loadMesh(elem, document, root)) {
      return false;
    }
  }

  // Load Groups (hierarchical extension)
  for (auto *elem = objectsElem->FirstChildElement("Group"); elem;
       elem = elem->NextSiblingElement("Group")) {
    if (!loadGroup(elem, document, root)) {
      return false;
    }
  }

  return true;
}

bool XMLSceneSerializer::loadSphere(tinyxml2::XMLElement *elem,
                                    SceneDocument *document,
                                    SceneNode *parent) {
  QString name = QString::fromStdString(
      elem->Attribute("name") ? elem->Attribute("name") : "Sphere");

  auto node = SceneNode::createSphere(name);

  // Read transform
  if (auto *posElem = elem->FirstChildElement("Position")) {
    node->transform()->setPosition(readVec3(posElem));
  }
  if (auto *rotElem = elem->FirstChildElement("Rotation")) {
    node->transform()->setRotation(readVec3(rotElem));
  }
  if (auto *scaleElem = elem->FirstChildElement("Scale")) {
    node->transform()->setScale(readVec3(scaleElem));
  }

  // Read geometry params
  GeometryParams params;
  if (auto *radiusElem = elem->FirstChildElement("Radius")) {
    params.radius = radiusElem->FloatAttribute("value", 0.5f);
  }
  node->setGeometryParams(params);

  // Read material reference
  if (auto *matElem = elem->FirstChildElement("Material")) {
    QString matName = QString::fromStdString(
        matElem->Attribute("name") ? matElem->Attribute("name") : "");
    QUuid matId = materialIdFromName(matName, document);
    node->setMaterialId(matId);
  }

  document->addNode(std::move(node), parent);
  return true;
}

bool XMLSceneSerializer::loadTriangle(tinyxml2::XMLElement *elem,
                                      SceneDocument *document,
                                      SceneNode *parent) {
  QString name = QString::fromStdString(
      elem->Attribute("name") ? elem->Attribute("name") : "Triangle");

  auto node = SceneNode::createTriangle(name);

  // Read vertices
  GeometryParams params;
  if (auto *v0Elem = elem->FirstChildElement("V0")) {
    params.v0 = readVec3(v0Elem);
  }
  if (auto *v1Elem = elem->FirstChildElement("V1")) {
    params.v1 = readVec3(v1Elem);
  }
  if (auto *v2Elem = elem->FirstChildElement("V2")) {
    params.v2 = readVec3(v2Elem);
  }
  node->setGeometryParams(params);

  // Read material reference
  if (auto *matElem = elem->FirstChildElement("Material")) {
    QString matName = QString::fromStdString(
        matElem->Attribute("name") ? matElem->Attribute("name") : "");
    QUuid matId = materialIdFromName(matName, document);
    node->setMaterialId(matId);
  }

  document->addNode(std::move(node), parent);
  return true;
}

bool XMLSceneSerializer::loadMesh(tinyxml2::XMLElement *elem,
                                  SceneDocument *document, SceneNode *parent) {
  QString name = QString::fromStdString(
      elem->Attribute("name") ? elem->Attribute("name") : "Mesh");

  auto node = SceneNode::createMesh(name);

  // Read transform
  if (auto *posElem = elem->FirstChildElement("Position")) {
    node->transform()->setPosition(readVec3(posElem));
  }
  if (auto *rotElem = elem->FirstChildElement("Rotation")) {
    node->transform()->setRotation(readVec3(rotElem));
  }
  if (auto *scaleElem = elem->FirstChildElement("Scale")) {
    node->transform()->setScale(readVec3(scaleElem));
  }

  // Read mesh file
  GeometryParams params;
  if (auto *fileElem = elem->FirstChildElement("File")) {
    params.meshFilePath = QString::fromStdString(
        fileElem->Attribute("name") ? fileElem->Attribute("name") : "");
  }
  node->setGeometryParams(params);

  // Read material reference
  if (auto *matElem = elem->FirstChildElement("Material")) {
    QString matName = QString::fromStdString(
        matElem->Attribute("name") ? matElem->Attribute("name") : "");
    QUuid matId = materialIdFromName(matName, document);
    node->setMaterialId(matId);
  }

  document->addNode(std::move(node), parent);
  return true;
}

bool XMLSceneSerializer::loadGroup(tinyxml2::XMLElement *elem,
                                   SceneDocument *document, SceneNode *parent) {
  QString name = QString::fromStdString(
      elem->Attribute("name") ? elem->Attribute("name") : "Group");

  auto node = SceneNode::createGroup(name);

  // Read transform
  if (auto *posElem = elem->FirstChildElement("Position")) {
    node->transform()->setPosition(readVec3(posElem));
  }
  if (auto *rotElem = elem->FirstChildElement("Rotation")) {
    node->transform()->setRotation(readVec3(rotElem));
  }
  if (auto *scaleElem = elem->FirstChildElement("Scale")) {
    node->transform()->setScale(readVec3(scaleElem));
  }

  SceneNode *groupNode = document->addNode(std::move(node), parent);

  // Load children recursively
  for (auto *childElem = elem->FirstChildElement("Sphere"); childElem;
       childElem = childElem->NextSiblingElement("Sphere")) {
    if (!loadSphere(childElem, document, groupNode))
      return false;
  }
  for (auto *childElem = elem->FirstChildElement("Triangle"); childElem;
       childElem = childElem->NextSiblingElement("Triangle")) {
    if (!loadTriangle(childElem, document, groupNode))
      return false;
  }
  for (auto *childElem = elem->FirstChildElement("Mesh"); childElem;
       childElem = childElem->NextSiblingElement("Mesh")) {
    if (!loadMesh(childElem, document, groupNode))
      return false;
  }
  for (auto *childElem = elem->FirstChildElement("Group"); childElem;
       childElem = childElem->NextSiblingElement("Group")) {
    if (!loadGroup(childElem, document, groupNode))
      return false;
  }

  return true;
}

// ===== Saving Helpers =====

void XMLSceneSerializer::saveConfig(tinyxml2::XMLDocument &doc,
                                    tinyxml2::XMLElement *root,
                                    const SceneDocument *document) {
  const RenderConfig &config = document->renderConfig();

  tinyxml2::XMLElement *configElem = doc.NewElement("Config");
  root->InsertEndChild(configElem);

  tinyxml2::XMLElement *widthElem = doc.NewElement("Width");
  widthElem->SetAttribute("value", config.width);
  configElem->InsertEndChild(widthElem);

  // Calculate aspect ratio from width/height
  float aspectRatio =
      static_cast<float>(config.width) / static_cast<float>(config.height);
  tinyxml2::XMLElement *aspectElem = doc.NewElement("Aspect_ratio");
  aspectElem->SetAttribute("value", aspectRatio);
  configElem->InsertEndChild(aspectElem);

  tinyxml2::XMLElement *samplesElem = doc.NewElement("Samples_Per_Pixel");
  samplesElem->SetAttribute("value", config.samplesPerPixel);
  configElem->InsertEndChild(samplesElem);

  tinyxml2::XMLElement *depthElem = doc.NewElement("Max_Depth");
  depthElem->SetAttribute("value", config.maxDepth);
  configElem->InsertEndChild(depthElem);
}

void XMLSceneSerializer::saveCamera(tinyxml2::XMLDocument &doc,
                                    tinyxml2::XMLElement *root,
                                    const SceneDocument *document) {
  const CameraSettings &camera = document->camera();

  tinyxml2::XMLElement *cameraElem = doc.NewElement("Camera");
  root->InsertEndChild(cameraElem);

  writeVec3(doc, cameraElem, "Look_From", camera.lookFrom);
  writeVec3(doc, cameraElem, "Look_at", camera.lookAt);
  writeVec3(doc, cameraElem, "Up", camera.up);

  tinyxml2::XMLElement *fovElem = doc.NewElement("FOV");
  fovElem->SetAttribute("angle", camera.fov);
  cameraElem->InsertEndChild(fovElem);
}

void XMLSceneSerializer::saveLights(tinyxml2::XMLDocument &doc,
                                    tinyxml2::XMLElement *root,
                                    const SceneDocument *document) {
  const SunSettings &sunSettings = document->sun();

  tinyxml2::XMLElement *lightsElem = doc.NewElement("Lights");
  root->InsertEndChild(lightsElem);

  tinyxml2::XMLElement *sunElem = doc.NewElement("Sun");
  lightsElem->InsertEndChild(sunElem);

  writeVec3(doc, sunElem, "Direction", sunSettings.direction);

  tinyxml2::XMLElement *intensityElem = doc.NewElement("Intensity");
  intensityElem->SetAttribute("value", sunSettings.intensity);
  sunElem->InsertEndChild(intensityElem);

  writeVec3Rgb(doc, sunElem, "Color", sunSettings.color);
}

void XMLSceneSerializer::saveMaterials(tinyxml2::XMLDocument &doc,
                                       tinyxml2::XMLElement *root,
                                       const SceneDocument *document) {
  tinyxml2::XMLElement *materialsElem = doc.NewElement("Materials");
  root->InsertEndChild(materialsElem);

  for (const auto &mat : document->materials()) {
    tinyxml2::XMLElement *matElem = nullptr;

    switch (mat->type()) {
    case MaterialType::Lambertian:
      matElem = doc.NewElement("Lambertian");
      matElem->SetAttribute("name", mat->name().toStdString().c_str());
      writeVec3Rgb(doc, matElem, "Color", mat->color());
      break;

    case MaterialType::Metal:
      matElem = doc.NewElement("Metal");
      matElem->SetAttribute("name", mat->name().toStdString().c_str());
      writeVec3Rgb(doc, matElem, "Color", mat->color());
      {
        tinyxml2::XMLElement *fuzzElem = doc.NewElement("Fuzz");
        fuzzElem->SetAttribute("value", mat->fuzz());
        matElem->InsertEndChild(fuzzElem);
      }
      break;

    case MaterialType::Emissive:
      matElem = doc.NewElement("Emissive");
      matElem->SetAttribute("name", mat->name().toStdString().c_str());
      writeVec3Rgb(doc, matElem, "Color", mat->color());
      {
        tinyxml2::XMLElement *strengthElem = doc.NewElement("Strength");
        strengthElem->SetAttribute("value", mat->emissiveStrength());
        matElem->InsertEndChild(strengthElem);
      }
      break;

    case MaterialType::Dielectric:
      matElem = doc.NewElement("Dielectric");
      matElem->SetAttribute("name", mat->name().toStdString().c_str());
      writeVec3Rgb(doc, matElem, "Color", mat->color());
      {
        tinyxml2::XMLElement *iorElem = doc.NewElement("IOR");
        iorElem->SetAttribute("value", mat->refractiveIndex());
        matElem->InsertEndChild(iorElem);
      }
      break;

    default:
      continue;
    }

    if (matElem) {
      materialsElem->InsertEndChild(matElem);
    }
  }
}

void XMLSceneSerializer::saveObjects(tinyxml2::XMLDocument &doc,
                                     tinyxml2::XMLElement *root,
                                     const SceneDocument *document) {
  tinyxml2::XMLElement *objectsElem = doc.NewElement("Objects");
  root->InsertEndChild(objectsElem);

  // Save all root-level nodes
  const SceneNode *rootNode = document->rootNode();
  if (rootNode) {
    for (SceneNode *child : rootNode->children()) {
      saveNode(doc, objectsElem, child, document);
    }
  }
}

void XMLSceneSerializer::saveNode(tinyxml2::XMLDocument &doc,
                                  tinyxml2::XMLElement *parent,
                                  const SceneNode *node,
                                  const SceneDocument *document) {
  tinyxml2::XMLElement *elem = nullptr;

  switch (node->geometryType()) {
  case GeometryType::None:
    // Group node
    elem = doc.NewElement("Group");
    elem->SetAttribute("name", node->name().toStdString().c_str());
    writeVec3(doc, elem, "Position", node->transform()->position());
    writeVec3(doc, elem, "Rotation", node->transform()->rotation());
    writeVec3(doc, elem, "Scale", node->transform()->scale());
    // Save children recursively
    for (SceneNode *child : node->children()) {
      saveNode(doc, elem, child, document);
    }
    break;

  case GeometryType::Sphere:
    elem = doc.NewElement("Sphere");
    elem->SetAttribute("name", node->name().toStdString().c_str());
    {
      tinyxml2::XMLElement *radiusElem = doc.NewElement("Radius");
      radiusElem->SetAttribute("value", node->geometryParams().radius);
      elem->InsertEndChild(radiusElem);
    }
    writeVec3(doc, elem, "Position", node->transform()->position());
    writeVec3(doc, elem, "Scale", node->transform()->scale());
    writeVec3(doc, elem, "Rotation", node->transform()->rotation());
    {
      tinyxml2::XMLElement *matElem = doc.NewElement("Material");
      matElem->SetAttribute("name",
                            materialNameFromId(node->materialId(), document)
                                .toStdString()
                                .c_str());
      elem->InsertEndChild(matElem);
    }
    break;

  case GeometryType::Triangle:
    elem = doc.NewElement("Triangle");
    elem->SetAttribute("name", node->name().toStdString().c_str());
    writeVec3(doc, elem, "V0", node->geometryParams().v0);
    writeVec3(doc, elem, "V1", node->geometryParams().v1);
    writeVec3(doc, elem, "V2", node->geometryParams().v2);
    {
      tinyxml2::XMLElement *matElem = doc.NewElement("Material");
      matElem->SetAttribute("name",
                            materialNameFromId(node->materialId(), document)
                                .toStdString()
                                .c_str());
      elem->InsertEndChild(matElem);
    }
    break;

  case GeometryType::Mesh:
    elem = doc.NewElement("Mesh");
    elem->SetAttribute("name", node->name().toStdString().c_str());
    writeVec3(doc, elem, "Position", node->transform()->position());
    writeVec3(doc, elem, "Scale", node->transform()->scale());
    writeVec3(doc, elem, "Rotation", node->transform()->rotation());
    {
      tinyxml2::XMLElement *matElem = doc.NewElement("Material");
      matElem->SetAttribute("name",
                            materialNameFromId(node->materialId(), document)
                                .toStdString()
                                .c_str());
      elem->InsertEndChild(matElem);
    }
    {
      tinyxml2::XMLElement *fileElem = doc.NewElement("File");
      fileElem->SetAttribute(
          "name", node->geometryParams().meshFilePath.toStdString().c_str());
      elem->InsertEndChild(fileElem);
    }
    break;

  default:
    return;
  }

  if (elem) {
    parent->InsertEndChild(elem);
  }
}

// ===== Utility Helpers =====

glm::vec3 XMLSceneSerializer::readVec3(tinyxml2::XMLElement *elem,
                                       const char *xAttr, const char *yAttr,
                                       const char *zAttr) {
  return glm::vec3(elem->FloatAttribute(xAttr, 0.0f),
                   elem->FloatAttribute(yAttr, 0.0f),
                   elem->FloatAttribute(zAttr, 0.0f));
}

void XMLSceneSerializer::writeVec3(tinyxml2::XMLDocument &doc,
                                   tinyxml2::XMLElement *parent,
                                   const char *name, const glm::vec3 &v,
                                   const char *xAttr, const char *yAttr,
                                   const char *zAttr) {
  tinyxml2::XMLElement *elem = doc.NewElement(name);
  elem->SetAttribute(xAttr, v.x);
  elem->SetAttribute(yAttr, v.y);
  elem->SetAttribute(zAttr, v.z);
  parent->InsertEndChild(elem);
}

void XMLSceneSerializer::writeVec3Rgb(tinyxml2::XMLDocument &doc,
                                      tinyxml2::XMLElement *parent,
                                      const char *name, const glm::vec3 &v) {
  tinyxml2::XMLElement *elem = doc.NewElement(name);
  elem->SetAttribute("r", v.r);
  elem->SetAttribute("g", v.g);
  elem->SetAttribute("b", v.b);
  parent->InsertEndChild(elem);
}

glm::vec3 XMLSceneSerializer::readColorRgb(tinyxml2::XMLElement *elem) {
  float r = 0.0f, g = 0.0f, b = 0.0f;

  const char *rAttr = elem->Attribute("r");
  const char *gAttr = elem->Attribute("g");
  const char *bAttr = elem->Attribute("b");

  if (rAttr && strlen(rAttr) > 0)
    r = elem->FloatAttribute("r", 0.0f);
  if (gAttr && strlen(gAttr) > 0)
    g = elem->FloatAttribute("g", 0.0f);
  if (bAttr && strlen(bAttr) > 0)
    b = elem->FloatAttribute("b", 0.0f);

  return glm::vec3(r, g, b);
}

QString XMLSceneSerializer::materialNameFromId(const QUuid &id,
                                               const SceneDocument *document) {
  if (id.isNull())
    return QString();

  if (auto *mat = document->findMaterial(id)) {
    return mat->name();
  }
  return QString();
}

QUuid XMLSceneSerializer::materialIdFromName(const QString &name,
                                             const SceneDocument *document) {
  if (name.isEmpty())
    return QUuid();

  for (const auto &mat : document->materials()) {
    if (mat->name() == name) {
      return mat->uuid();
    }
  }
  return QUuid();
}

// ===== Registration =====

void registerXMLSceneSerializer() {
  SceneSerializerFactory::instance().registerSerializer(
      std::make_unique<XMLSceneSerializer>());
}

} // namespace scene
} // namespace Raytracer
