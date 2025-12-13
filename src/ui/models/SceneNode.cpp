#include "SceneNode.h"
#include "MaterialDefinition.h"
#include <algorithm>

namespace Raytracer {
namespace scene {

SceneNode::SceneNode(QObject *parent)
    : QObject(parent)
    , m_uuid(QUuid::createUuid())
{
    connectTransformSignals();
}

SceneNode::SceneNode(const QString &name, GeometryType type, QObject *parent)
    : QObject(parent)
    , m_uuid(QUuid::createUuid())
    , m_name(name)
    , m_geometryType(type)
{
    connectTransformSignals();
}

SceneNode::~SceneNode()
{
    // Note: Children are not owned by this node in the Qt sense,
    // they are managed by SceneDocument
}

void SceneNode::connectTransformSignals()
{
    connect(&m_transform, &Transform::changed, this, &SceneNode::changed);
}

void SceneNode::setName(const QString &name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged(name);
        emit changed();
    }
}

void SceneNode::setGeometryType(GeometryType type)
{
    if (m_geometryType != type) {
        m_geometryType = type;
        emit geometryChanged();
        emit changed();
    }
}

void SceneNode::setGeometryParams(const GeometryParams &params)
{
    m_geometryParams = params;
    emit geometryChanged();
    emit changed();
}

void SceneNode::setMaterialId(const QUuid &id)
{
    if (m_materialId != id) {
        m_materialId = id;
        emit materialChanged(id);
        emit changed();
    }
}

void SceneNode::setVisible(bool visible)
{
    if (m_visible != visible) {
        m_visible = visible;
        emit visibilityChanged(visible);
        emit changed();
    }
}

void SceneNode::setLocked(bool locked)
{
    if (m_locked != locked) {
        m_locked = locked;
        emit changed();
    }
}

void SceneNode::setParentNode(SceneNode *parent)
{
    if (m_parentNode != parent) {
        // Remove from old parent
        if (m_parentNode) {
            m_parentNode->removeChild(this);
        }
        
        m_parentNode = parent;
        
        // Update transform hierarchy
        if (parent) {
            m_transform.setParentTransform(parent->transform());
        } else {
            m_transform.setParentTransform(nullptr);
        }
        
        emit hierarchyChanged();
        emit changed();
    }
}

void SceneNode::addChild(SceneNode *child)
{
    if (!child || child == this) return;
    
    // Check if already a child
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) return;
    
    // Remove from current parent
    if (child->m_parentNode && child->m_parentNode != this) {
        child->m_parentNode->removeChild(child);
    }
    
    m_children.push_back(child);
    child->m_parentNode = this;
    child->m_transform.setParentTransform(&m_transform);
    
    emit hierarchyChanged();
    emit changed();
}

void SceneNode::removeChild(SceneNode *child)
{
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        (*it)->m_parentNode = nullptr;
        (*it)->m_transform.setParentTransform(nullptr);
        m_children.erase(it);
        
        emit hierarchyChanged();
        emit changed();
    }
}

void SceneNode::clearChildren()
{
    for (auto *child : m_children) {
        child->m_parentNode = nullptr;
        child->m_transform.setParentTransform(nullptr);
    }
    m_children.clear();
    
    emit hierarchyChanged();
    emit changed();
}

SceneNode *SceneNode::childAt(int index) const
{
    if (index >= 0 && index < static_cast<int>(m_children.size())) {
        return m_children[index];
    }
    return nullptr;
}

int SceneNode::indexOfChild(SceneNode *child) const
{
    auto it = std::find(m_children.begin(), m_children.end(), child);
    if (it != m_children.end()) {
        return static_cast<int>(std::distance(m_children.begin(), it));
    }
    return -1;
}

void SceneNode::computeBounds(glm::vec3 &outMin, glm::vec3 &outMax) const
{
    glm::mat4 world = m_transform.worldMatrix();
    glm::vec3 center = glm::vec3(world[3]);
    glm::vec3 scale = m_transform.scale();
    
    switch (m_geometryType) {
        case GeometryType::Sphere: {
            float r = m_geometryParams.radius * std::max({scale.x, scale.y, scale.z});
            outMin = center - glm::vec3(r);
            outMax = center + glm::vec3(r);
            break;
        }
        case GeometryType::Triangle: {
            // Transform vertices to world space
            glm::vec3 v0 = glm::vec3(world * glm::vec4(m_geometryParams.v0, 1.0f));
            glm::vec3 v1 = glm::vec3(world * glm::vec4(m_geometryParams.v1, 1.0f));
            glm::vec3 v2 = glm::vec3(world * glm::vec4(m_geometryParams.v2, 1.0f));
            outMin = glm::min(glm::min(v0, v1), v2);
            outMax = glm::max(glm::max(v0, v1), v2);
            break;
        }
        case GeometryType::Plane: {
            float hw = m_geometryParams.planeWidth * 0.5f * scale.x;
            float hh = m_geometryParams.planeHeight * 0.5f * scale.z;
            outMin = center - glm::vec3(hw, 0.0f, hh);
            outMax = center + glm::vec3(hw, 0.01f, hh); // Thin box
            break;
        }
        case GeometryType::Cube: {
            float hs = m_geometryParams.cubeSize * 0.5f;
            glm::vec3 halfSize(hs * scale.x, hs * scale.y, hs * scale.z);
            outMin = center - halfSize;
            outMax = center + halfSize;
            break;
        }
        case GeometryType::None:
        case GeometryType::Mesh:
        default: {
            // Default small bounds
            outMin = center - glm::vec3(0.1f);
            outMax = center + glm::vec3(0.1f);
            break;
        }
    }
}

glm::vec3 SceneNode::worldCenter() const
{
    glm::mat4 world = m_transform.worldMatrix();
    return glm::vec3(world[3]);
}

std::unique_ptr<SceneNode> SceneNode::clone() const
{
    auto copy = std::make_unique<SceneNode>(m_name + " (Copy)", m_geometryType);
    copy->m_geometryParams = m_geometryParams;
    copy->m_transform.copyFrom(m_transform);
    copy->m_materialId = m_materialId;
    copy->m_visible = m_visible;
    copy->m_locked = m_locked;
    // Note: Children are not cloned here - caller should handle hierarchy
    return copy;
}

std::unique_ptr<SceneNode> SceneNode::createSphere(const QString &name, float radius)
{
    auto node = std::make_unique<SceneNode>(name, GeometryType::Sphere);
    node->m_geometryParams.radius = radius;
    return node;
}

std::unique_ptr<SceneNode> SceneNode::createTriangle(const QString &name)
{
    auto node = std::make_unique<SceneNode>(name, GeometryType::Triangle);
    // Default triangle vertices already set in GeometryParams
    return node;
}

std::unique_ptr<SceneNode> SceneNode::createPlane(const QString &name, float width, float height)
{
    auto node = std::make_unique<SceneNode>(name, GeometryType::Plane);
    node->m_geometryParams.planeWidth = width;
    node->m_geometryParams.planeHeight = height;
    return node;
}

std::unique_ptr<SceneNode> SceneNode::createCube(const QString &name, float size)
{
    auto node = std::make_unique<SceneNode>(name, GeometryType::Cube);
    node->m_geometryParams.cubeSize = size;
    return node;
}

std::unique_ptr<SceneNode> SceneNode::createMesh(const QString &name, const QString &meshFile)
{
    auto node = std::make_unique<SceneNode>(name, GeometryType::Mesh);
    node->m_geometryParams.meshFilePath = meshFile;
    return node;
}

std::unique_ptr<SceneNode> SceneNode::createGroup(const QString &name)
{
    return std::make_unique<SceneNode>(name, GeometryType::None);
}

QString SceneNode::geometryTypeToString(GeometryType type)
{
    switch (type) {
        case GeometryType::None:     return QStringLiteral("Group");
        case GeometryType::Sphere:   return QStringLiteral("Sphere");
        case GeometryType::Triangle: return QStringLiteral("Triangle");
        case GeometryType::Plane:    return QStringLiteral("Plane");
        case GeometryType::Cube:     return QStringLiteral("Cube");
        case GeometryType::Mesh:     return QStringLiteral("Mesh");
    }
    return QStringLiteral("Unknown");
}

GeometryType SceneNode::stringToGeometryType(const QString &str)
{
    if (str.compare(QLatin1String("Sphere"), Qt::CaseInsensitive) == 0)
        return GeometryType::Sphere;
    if (str.compare(QLatin1String("Triangle"), Qt::CaseInsensitive) == 0)
        return GeometryType::Triangle;
    if (str.compare(QLatin1String("Plane"), Qt::CaseInsensitive) == 0)
        return GeometryType::Plane;
    if (str.compare(QLatin1String("Cube"), Qt::CaseInsensitive) == 0)
        return GeometryType::Cube;
    if (str.compare(QLatin1String("Mesh"), Qt::CaseInsensitive) == 0)
        return GeometryType::Mesh;
    return GeometryType::None;
}

} // namespace scene
} // namespace Raytracer
