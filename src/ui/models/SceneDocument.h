#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QUuid>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Raytracer {
namespace scene {

class SceneNode;
class MaterialDefinition;
class Transform;

/**
 * @brief Camera settings for the scene
 */
struct CameraSettings {
  // Camera positioned to view origin from a standard 3/4 angle
  // This matches the SceneViewport defaults (distance=10, pitch=-30, yaw=45)
  glm::vec3 lookFrom{5.0f, 5.0f, 5.0f}; // Position matching UI viewport
  glm::vec3 lookAt{0.0f, 0.0f, 0.0f};   // Look at origin (matches UI viewport)
  glm::vec3 up{0.0f, 1.0f, 0.0f};
  float fov{45.0f}; // Match UI perspective FOV
  float focalLength{1.0f};
  float aperture{0.0f}; // 0 = no depth of field
  float focusDistance{10.0f};
};

/**
 * @brief Sun/directional light settings
 */
struct SunSettings {
  glm::vec3 direction{0.4f, 1.2f, 0.3f};
  glm::vec3 color{1.0f, 0.98f, 0.95f};
  float intensity{1.0f};
};

/**
 * @brief Render configuration settings
 */
struct RenderConfig {
  int width{800};
  int height{450};
  int samplesPerPixel{20};
  int maxDepth{10};
  bool useBVH{true};
};

/**
 * @brief The main scene document containing all scene data.
 *
 * SceneDocument is the central data model for the scene editor.
 * It owns all scene nodes, materials, and settings, and provides
 * the interface for scene manipulation.
 *
 * Key responsibilities:
 * - Scene graph management (nodes, hierarchy)
 * - Material library
 * - Camera and lighting settings
 * - Render configuration
 * - Document state (dirty flag, file path)
 * - Signals for UI updates
 */
class SceneDocument : public QObject {
  Q_OBJECT

public:
  explicit SceneDocument(QObject *parent = nullptr);
  ~SceneDocument() override;

  // ===== Document State =====

  QString filePath() const { return m_filePath; }
  void setFilePath(const QString &path);

  bool isDirty() const { return m_dirty; }
  void setDirty(bool dirty);
  void markDirty() { setDirty(true); }

  QString documentName() const;

  // ===== Scene Nodes =====

  // Root node (always exists, represents scene root)
  SceneNode *rootNode() { return m_rootNode.get(); }
  const SceneNode *rootNode() const { return m_rootNode.get(); }

  // All nodes in the scene (flat list for iteration)
  const std::vector<std::unique_ptr<SceneNode>> &allNodes() const {
    return m_allNodes;
  }
  int nodeCount() const { return static_cast<int>(m_allNodes.size()); }

  // Add a new node (takes ownership)
  SceneNode *addNode(std::unique_ptr<SceneNode> node,
                     SceneNode *parent = nullptr);

  // Remove a node (and all its children)
  void removeNode(SceneNode *node);

  // Find node by UUID
  SceneNode *findNode(const QUuid &uuid);
  const SceneNode *findNode(const QUuid &uuid) const;

  // Find node by name (first match)
  SceneNode *findNodeByName(const QString &name);

  // Reparent a node
  void reparentNode(SceneNode *node, SceneNode *newParent);

  // ===== Materials =====

  const std::vector<std::unique_ptr<MaterialDefinition>> &materials() const {
    return m_materials;
  }
  int materialCount() const { return static_cast<int>(m_materials.size()); }

  // Add a material (takes ownership)
  MaterialDefinition *addMaterial(std::unique_ptr<MaterialDefinition> material);

  // Remove a material
  void removeMaterial(const QUuid &uuid);

  // Find material by UUID
  MaterialDefinition *findMaterial(const QUuid &uuid);
  const MaterialDefinition *findMaterial(const QUuid &uuid) const;

  // Find material by name
  MaterialDefinition *findMaterialByName(const QString &name);

  // Get default material (created if needed)
  MaterialDefinition *defaultMaterial();

  // ===== Camera =====

  CameraSettings &camera() { return m_camera; }
  const CameraSettings &camera() const { return m_camera; }
  void setCamera(const CameraSettings &settings);

  // ===== Lighting =====

  SunSettings &sun() { return m_sun; }
  const SunSettings &sun() const { return m_sun; }
  void setSun(const SunSettings &settings);

  // ===== Render Config =====

  RenderConfig &renderConfig() { return m_renderConfig; }
  const RenderConfig &renderConfig() const { return m_renderConfig; }
  void setRenderConfig(const RenderConfig &config);

  // ===== Selection (convenience, actual selection may be in controller) =====

  SceneNode *selectedNode() { return m_selectedNode; }
  const SceneNode *selectedNode() const { return m_selectedNode; }
  void setSelectedNode(SceneNode *node);

  // ===== Document Operations =====

  // Create a new empty scene
  void newScene();

  // Create a default scene with ground and sample objects
  void createDefaultScene();

  // Clear all content
  void clear();

  // Statistics
  struct Statistics {
    int totalNodes;
    int sphereCount;
    int triangleCount;
    int meshCount;
    int materialCount;
  };
  Statistics computeStatistics() const;

signals:
  void documentChanged();
  void filePathChanged(const QString &path);
  void dirtyChanged(bool dirty);

  void nodeAdded(SceneNode *node);
  void nodeRemoved(const QUuid &uuid);
  void nodeChanged(SceneNode *node);
  void hierarchyChanged();

  void materialAdded(MaterialDefinition *material);
  void materialRemoved(const QUuid &uuid);
  void materialChanged(MaterialDefinition *material);

  void cameraChanged();
  void lightingChanged();
  void renderConfigChanged();

  void selectionChanged(SceneNode *node);

private:
  void setupDefaultMaterials();
  void connectNodeSignals(SceneNode *node);

  QString m_filePath;
  bool m_dirty{false};

  // Scene graph
  std::unique_ptr<SceneNode> m_rootNode;
  std::vector<std::unique_ptr<SceneNode>> m_allNodes;
  QHash<QUuid, SceneNode *> m_nodeMap;

  // Materials
  std::vector<std::unique_ptr<MaterialDefinition>> m_materials;
  QHash<QUuid, MaterialDefinition *> m_materialMap;
  QUuid m_defaultMaterialId;

  // Settings
  CameraSettings m_camera;
  SunSettings m_sun;
  RenderConfig m_renderConfig;

  // Selection
  SceneNode *m_selectedNode{nullptr};
};

} // namespace scene
} // namespace Raytracer
