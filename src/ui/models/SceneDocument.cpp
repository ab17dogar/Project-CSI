#include "SceneDocument.h"
#include "DesignSpaceFactory.h"
#include "MaterialDefinition.h"
#include "SceneNode.h"
#include <QFileInfo>
#include <algorithm>

namespace Raytracer {
namespace scene {

SceneDocument::SceneDocument(QObject *parent) : QObject(parent) {
  // Create root node
  m_rootNode =
      std::make_unique<SceneNode>("Scene", GeometryType::None, nullptr);

  // Setup default materials
  setupDefaultMaterials();
}

SceneDocument::~SceneDocument() = default;

void SceneDocument::setFilePath(const QString &path) {
  if (m_filePath != path) {
    m_filePath = path;
    emit filePathChanged(path);
  }
}

void SceneDocument::setDirty(bool dirty) {
  if (m_dirty != dirty) {
    m_dirty = dirty;
    emit dirtyChanged(dirty);
  }
}

QString SceneDocument::documentName() const {
  if (m_filePath.isEmpty()) {
    return QStringLiteral("Untitled");
  }
  return QFileInfo(m_filePath).baseName();
}

SceneNode *SceneDocument::addNode(std::unique_ptr<SceneNode> node,
                                  SceneNode *parent) {
  if (!node)
    return nullptr;

  SceneNode *rawPtr = node.get();

  // Set parent (defaults to root)
  if (!parent) {
    parent = m_rootNode.get();
  }
  parent->addChild(rawPtr);

  // Add to flat list and map
  m_nodeMap[rawPtr->uuid()] = rawPtr;
  m_allNodes.push_back(std::move(node));

  // Connect signals
  connectNodeSignals(rawPtr);

  markDirty();
  emit nodeAdded(rawPtr);
  emit hierarchyChanged();

  return rawPtr;
}

void SceneDocument::removeNode(SceneNode *node) {
  if (!node || node == m_rootNode.get())
    return;

  QUuid uuid = node->uuid();

  // Remove from selection if selected
  if (m_selectedNode == node) {
    setSelectedNode(nullptr);
  }

  // Recursively remove children first
  auto children = node->children(); // Copy to avoid iterator invalidation
  for (auto *child : children) {
    removeNode(child);
  }

  // Remove from parent
  if (node->parentNode()) {
    node->parentNode()->removeChild(node);
  }

  // Remove from map
  m_nodeMap.remove(uuid);

  // Remove from vector
  auto it = std::find_if(m_allNodes.begin(), m_allNodes.end(),
                         [node](const std::unique_ptr<SceneNode> &ptr) {
                           return ptr.get() == node;
                         });
  if (it != m_allNodes.end()) {
    m_allNodes.erase(it);
  }

  markDirty();
  emit nodeRemoved(uuid);
  emit hierarchyChanged();
}

SceneNode *SceneDocument::findNode(const QUuid &uuid) {
  auto it = m_nodeMap.constFind(uuid);
  return (it != m_nodeMap.constEnd()) ? it.value() : nullptr;
}

const SceneNode *SceneDocument::findNode(const QUuid &uuid) const {
  auto it = m_nodeMap.constFind(uuid);
  return (it != m_nodeMap.constEnd()) ? it.value() : nullptr;
}

SceneNode *SceneDocument::findNodeByName(const QString &name) {
  for (auto &node : m_allNodes) {
    if (node->name() == name) {
      return node.get();
    }
  }
  return nullptr;
}

void SceneDocument::reparentNode(SceneNode *node, SceneNode *newParent) {
  if (!node || node == m_rootNode.get())
    return;
  if (!newParent)
    newParent = m_rootNode.get();

  // Prevent circular hierarchy
  SceneNode *check = newParent;
  while (check) {
    if (check == node)
      return; // Would create cycle
    check = check->parentNode();
  }

  node->setParentNode(newParent);

  markDirty();
  emit hierarchyChanged();
}

MaterialDefinition *
SceneDocument::addMaterial(std::unique_ptr<MaterialDefinition> material) {
  if (!material)
    return nullptr;

  MaterialDefinition *rawPtr = material.get();

  m_materialMap[rawPtr->uuid()] = rawPtr;
  m_materials.push_back(std::move(material));

  // Connect signals
  connect(rawPtr, &MaterialDefinition::changed, this, [this, rawPtr]() {
    markDirty();
    emit materialChanged(rawPtr);
  });

  markDirty();
  emit materialAdded(rawPtr);

  return rawPtr;
}

void SceneDocument::removeMaterial(const QUuid &uuid) {
  // Don't remove default material
  if (uuid == m_defaultMaterialId)
    return;

  auto it =
      std::find_if(m_materials.begin(), m_materials.end(),
                   [&uuid](const std::unique_ptr<MaterialDefinition> &ptr) {
                     return ptr->uuid() == uuid;
                   });

  if (it != m_materials.end()) {
    m_materialMap.remove(uuid);
    m_materials.erase(it);

    markDirty();
    emit materialRemoved(uuid);
  }
}

MaterialDefinition *SceneDocument::findMaterial(const QUuid &uuid) {
  auto it = m_materialMap.constFind(uuid);
  return (it != m_materialMap.constEnd()) ? it.value() : nullptr;
}

const MaterialDefinition *SceneDocument::findMaterial(const QUuid &uuid) const {
  auto it = m_materialMap.constFind(uuid);
  return (it != m_materialMap.constEnd()) ? it.value() : nullptr;
}

MaterialDefinition *SceneDocument::findMaterialByName(const QString &name) {
  for (auto &mat : m_materials) {
    if (mat->name() == name) {
      return mat.get();
    }
  }
  return nullptr;
}

MaterialDefinition *SceneDocument::defaultMaterial() {
  return findMaterial(m_defaultMaterialId);
}

void SceneDocument::setCamera(const CameraSettings &settings) {
  m_camera = settings;
  markDirty();
  emit cameraChanged();
}

void SceneDocument::setSun(const SunSettings &settings) {
  m_sun = settings;
  markDirty();
  emit lightingChanged();
}

PointLightSettings *
SceneDocument::addPointLight(const PointLightSettings &light) {
  PointLightSettings newLight = light;
  if (newLight.uuid.isNull()) {
    newLight.uuid = QUuid::createUuid();
  }
  if (newLight.name.isEmpty()) {
    newLight.name = QString("Point Light %1").arg(m_pointLights.size() + 1);
  }
  m_pointLights.push_back(newLight);
  markDirty();
  emit pointLightsChanged();
  return &m_pointLights.back();
}

void SceneDocument::removePointLight(const QUuid &uuid) {
  auto it = std::find_if(
      m_pointLights.begin(), m_pointLights.end(),
      [&uuid](const PointLightSettings &l) { return l.uuid == uuid; });
  if (it != m_pointLights.end()) {
    m_pointLights.erase(it);
    markDirty();
    emit pointLightsChanged();
  }
}

PointLightSettings *SceneDocument::findPointLight(const QUuid &uuid) {
  auto it = std::find_if(
      m_pointLights.begin(), m_pointLights.end(),
      [&uuid](const PointLightSettings &l) { return l.uuid == uuid; });
  return it != m_pointLights.end() ? &(*it) : nullptr;
}

void SceneDocument::updatePointLight(const QUuid &uuid,
                                     const PointLightSettings &settings) {
  if (auto *light = findPointLight(uuid)) {
    *light = settings;
    light->uuid = uuid; // Preserve UUID
    markDirty();
    emit pointLightsChanged();
  }
}

void SceneDocument::setRenderConfig(const RenderConfig &config) {
  m_renderConfig = config;
  markDirty();
  emit renderConfigChanged();
}

void SceneDocument::setSelectedNode(SceneNode *node) {
  if (m_selectedNode != node) {
    m_selectedNode = node;
    emit selectionChanged(node);
  }
}

void SceneDocument::newScene() {
  clear();
  m_filePath.clear();
  setDirty(false);
  emit filePathChanged(QString());
}

void SceneDocument::createDefaultScene() {
  // Use DesignSpaceFactory for scalable, reusable design space creation
  DesignSpaceFactory::applyPreset(this,
                                  DesignSpaceFactory::PresetType::IndoorRoom);
}

void SceneDocument::clear() {
  // Clear selection
  setSelectedNode(nullptr);

  // Clear nodes
  m_allNodes.clear();
  m_nodeMap.clear();

  // Recreate root node
  m_rootNode =
      std::make_unique<SceneNode>("Scene", GeometryType::None, nullptr);

  // Clear materials
  m_materials.clear();
  m_materialMap.clear();
  m_defaultMaterialId = QUuid();

  // Reset settings to defaults
  m_camera = CameraSettings();
  m_sun = SunSettings();
  m_renderConfig = RenderConfig();

  emit hierarchyChanged();
  emit documentChanged();
}

SceneDocument::Statistics SceneDocument::computeStatistics() const {
  Statistics stats{};
  stats.totalNodes = static_cast<int>(m_allNodes.size());
  stats.materialCount = static_cast<int>(m_materials.size());

  for (const auto &node : m_allNodes) {
    switch (node->geometryType()) {
    case GeometryType::Sphere:
      stats.sphereCount++;
      break;
    case GeometryType::Triangle:
      stats.triangleCount++;
      break;
    case GeometryType::Mesh:
      stats.meshCount++;
      break;
    default:
      break;
    }
  }

  return stats;
}

void SceneDocument::setupDefaultMaterials() {
  // Room materials
  auto floorWood = MaterialDefinition::createLambertian(
      "floor_wood", glm::vec3(0.55f, 0.35f, 0.2f)); // Warm wood color
  m_defaultMaterialId = floorWood->uuid();
  addMaterial(std::move(floorWood));

  addMaterial(MaterialDefinition::createLambertian(
      "wall_white", glm::vec3(0.92f, 0.9f, 0.88f))); // Off-white walls
  addMaterial(MaterialDefinition::createLambertian(
      "wall_accent", glm::vec3(0.4f, 0.5f, 0.6f))); // Blue-gray accent

  // Object materials (original presets)
  addMaterial(MaterialDefinition::createLambertian(
      "ground", glm::vec3(0.8f, 0.8f, 0.0f)));
  addMaterial(MaterialDefinition::createLambertian(
      "mattBrown", glm::vec3(0.7f, 0.3f, 0.3f)));
  addMaterial(MaterialDefinition::createMetal(
      "fuzzySilver", glm::vec3(0.8f, 0.8f, 0.8f), 0.3f));
  addMaterial(MaterialDefinition::createMetal(
      "shinyGold", glm::vec3(0.8f, 0.6f, 0.2f), 0.0f));
  addMaterial(MaterialDefinition::createEmissive(
      "emissive", glm::vec3(1.0f, 1.0f, 1.0f), 1.0f));
}

void SceneDocument::connectNodeSignals(SceneNode *node) {
  connect(node, &SceneNode::changed, this, [this, node]() {
    markDirty();
    emit nodeChanged(node);
  });
}

} // namespace scene
} // namespace Raytracer
