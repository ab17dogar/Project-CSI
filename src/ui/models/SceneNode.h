#pragma once

#include <QString>
#include <QUuid>
#include <QObject>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

#include "Transform.h"

namespace Raytracer {
namespace scene {

class MaterialDefinition;
class SceneDocument;

/**
 * @brief Types of geometry that a scene node can represent
 */
enum class GeometryType
{
    None,           // Group/empty node
    Sphere,
    Triangle,
    Plane,
    Cube,
    Mesh            // External mesh file
};

/**
 * @brief Geometry-specific parameters
 */
struct GeometryParams
{
    // Sphere
    float radius {0.5f};
    
    // Triangle vertices (local space)
    glm::vec3 v0 {-0.5f, 0.0f, 0.0f};
    glm::vec3 v1 {0.5f, 0.0f, 0.0f};
    glm::vec3 v2 {0.0f, 1.0f, 0.0f};
    
    // Plane
    float planeWidth {1.0f};
    float planeHeight {1.0f};
    
    // Cube
    float cubeSize {1.0f};
    
    // Mesh
    QString meshFilePath;
};

/**
 * @brief A node in the scene graph hierarchy.
 * 
 * SceneNode represents a single object in the scene with:
 * - Transform (position, rotation, scale)
 * - Optional geometry (sphere, triangle, mesh, etc.)
 * - Material reference
 * - Parent/child relationships for hierarchical transforms
 */
class SceneNode : public QObject
{
    Q_OBJECT

public:
    explicit SceneNode(QObject *parent = nullptr);
    SceneNode(const QString &name, GeometryType type, QObject *parent = nullptr);
    ~SceneNode() override;
    
    // Unique identifier
    QUuid uuid() const { return m_uuid; }
    
    // Display name
    QString name() const { return m_name; }
    void setName(const QString &name);
    
    // Geometry type and parameters
    GeometryType geometryType() const { return m_geometryType; }
    void setGeometryType(GeometryType type);
    
    GeometryParams &geometryParams() { return m_geometryParams; }
    const GeometryParams &geometryParams() const { return m_geometryParams; }
    void setGeometryParams(const GeometryParams &params);
    
    // Transform component
    Transform *transform() { return &m_transform; }
    const Transform *transform() const { return &m_transform; }
    
    // Material reference (UUID of material in MaterialLibrary)
    QUuid materialId() const { return m_materialId; }
    void setMaterialId(const QUuid &id);
    
    // Visibility
    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);
    
    // Locked (cannot be selected/edited)
    bool isLocked() const { return m_locked; }
    void setLocked(bool locked);
    
    // Hierarchy
    SceneNode *parentNode() const { return m_parentNode; }
    void setParentNode(SceneNode *parent);
    
    const std::vector<SceneNode*> &children() const { return m_children; }
    void addChild(SceneNode *child);
    void removeChild(SceneNode *child);
    void clearChildren();
    int childCount() const { return static_cast<int>(m_children.size()); }
    SceneNode *childAt(int index) const;
    int indexOfChild(SceneNode *child) const;
    
    // World-space bounds (for selection, BVH, etc.)
    void computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const;
    glm::vec3 worldCenter() const;
    
    // Clone this node (deep copy, new UUID)
    std::unique_ptr<SceneNode> clone() const;
    
    // Factory methods for common primitives
    static std::unique_ptr<SceneNode> createSphere(const QString &name, float radius = 0.5f);
    static std::unique_ptr<SceneNode> createTriangle(const QString &name);
    static std::unique_ptr<SceneNode> createPlane(const QString &name, float width = 1.0f, float height = 1.0f);
    static std::unique_ptr<SceneNode> createCube(const QString &name, float size = 1.0f);
    static std::unique_ptr<SceneNode> createMesh(const QString &name, const QString &meshFile = QString());
    static std::unique_ptr<SceneNode> createGroup(const QString &name);
    
    // String conversion for serialization
    static QString geometryTypeToString(GeometryType type);
    static GeometryType stringToGeometryType(const QString &str);

signals:
    void changed();
    void nameChanged(const QString &name);
    void geometryChanged();
    void materialChanged(const QUuid &materialId);
    void visibilityChanged(bool visible);
    void hierarchyChanged();

private:
    void connectTransformSignals();
    
    QUuid m_uuid;
    QString m_name {"Node"};
    GeometryType m_geometryType {GeometryType::None};
    GeometryParams m_geometryParams;
    Transform m_transform;
    QUuid m_materialId;
    bool m_visible {true};
    bool m_locked {false};
    
    SceneNode *m_parentNode {nullptr};
    std::vector<SceneNode*> m_children;
};

} // namespace scene
} // namespace Raytracer
