#include "SceneCommands.h"
#include "../models/SceneDocument.h"

namespace Raytracer {
namespace scene {

// ===== AddNodeCommand =====

AddNodeCommand::AddNodeCommand(SceneDocument *document, 
                               std::unique_ptr<SceneNode> node,
                               SceneNode *parent)
    : SceneCommand(document)
    , m_node(std::move(node))
    , m_parentId(parent ? parent->uuid() : QUuid())
{
    if (m_node) {
        m_nodeName = m_node->name();
    }
}

bool AddNodeCommand::execute()
{
    if (!m_node && !m_nodePtr) return false;
    
    SceneNode *parent = m_parentId.isNull() ? nullptr : document()->findNode(m_parentId);
    
    if (m_node) {
        m_nodePtr = document()->addNode(std::move(m_node), parent);
        return m_nodePtr != nullptr;
    }
    return false;
}

bool AddNodeCommand::undo()
{
    if (!m_nodePtr) return false;
    
    // Find in document and remove
    auto &nodes = const_cast<std::vector<std::unique_ptr<SceneNode>>&>(document()->allNodes());
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        if (it->get() == m_nodePtr) {
            m_node = std::move(*it);
            break;
        }
    }
    
    document()->removeNode(m_nodePtr);
    m_nodePtr = nullptr;
    return true;
}

QString AddNodeCommand::description() const
{
    return QStringLiteral("Add %1").arg(m_nodeName);
}

// ===== DeleteNodeCommand =====

DeleteNodeCommand::DeleteNodeCommand(SceneDocument *document, SceneNode *node)
    : SceneCommand(document)
    , m_nodePtr(node)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        if (node->parentNode()) {
            m_parentId = node->parentNode()->uuid();
            m_indexInParent = node->parentNode()->indexOfChild(node);
        }
    }
}

bool DeleteNodeCommand::execute()
{
    SceneNode *node = m_nodePtr ? m_nodePtr : document()->findNode(m_nodeId);
    if (!node) return false;
    
    // Store the node before removal
    auto &nodes = const_cast<std::vector<std::unique_ptr<SceneNode>>&>(document()->allNodes());
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        if (it->get() == node) {
            m_node = std::move(*it);
            break;
        }
    }
    
    document()->removeNode(node);
    m_nodePtr = nullptr;
    return true;
}

bool DeleteNodeCommand::undo()
{
    if (!m_node) return false;
    
    SceneNode *parent = m_parentId.isNull() ? nullptr : document()->findNode(m_parentId);
    m_nodePtr = document()->addNode(std::move(m_node), parent);
    return m_nodePtr != nullptr;
}

QString DeleteNodeCommand::description() const
{
    return QStringLiteral("Delete %1").arg(m_nodeName);
}

// ===== TransformCommand =====

TransformCommand::TransformCommand(SceneDocument *document, 
                                   SceneNode *node,
                                   const glm::vec3 &newPosition,
                                   const glm::vec3 &newRotation,
                                   const glm::vec3 &newScale)
    : SceneCommand(document)
    , m_newPosition(newPosition)
    , m_newRotation(newRotation)
    , m_newScale(newScale)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        m_oldPosition = node->transform()->position();
        m_oldRotation = node->transform()->rotation();
        m_oldScale = node->transform()->scale();
    }
}

bool TransformCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->transform()->setPosition(m_newPosition);
    node->transform()->setRotation(m_newRotation);
    node->transform()->setScale(m_newScale);
    return true;
}

bool TransformCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->transform()->setPosition(m_oldPosition);
    node->transform()->setRotation(m_oldRotation);
    node->transform()->setScale(m_oldScale);
    return true;
}

QString TransformCommand::description() const
{
    return QStringLiteral("Transform %1").arg(m_nodeName);
}

bool TransformCommand::canMergeWith(const ICommand *other) const
{
    if (other->typeId() != typeId()) return false;
    auto *cmd = static_cast<const TransformCommand*>(other);
    return cmd->m_nodeId == m_nodeId;
}

bool TransformCommand::mergeWith(const ICommand *other)
{
    auto *cmd = static_cast<const TransformCommand*>(other);
    m_newPosition = cmd->m_newPosition;
    m_newRotation = cmd->m_newRotation;
    m_newScale = cmd->m_newScale;
    return true;
}

// ===== SetPositionCommand =====

SetPositionCommand::SetPositionCommand(SceneDocument *document, 
                                       SceneNode *node,
                                       const glm::vec3 &newPosition)
    : SceneCommand(document)
    , m_newPosition(newPosition)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        m_oldPosition = node->transform()->position();
    }
}

bool SetPositionCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->transform()->setPosition(m_newPosition);
    return true;
}

bool SetPositionCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->transform()->setPosition(m_oldPosition);
    return true;
}

QString SetPositionCommand::description() const
{
    return QStringLiteral("Move %1").arg(m_nodeName);
}

bool SetPositionCommand::canMergeWith(const ICommand *other) const
{
    if (other->typeId() != typeId()) return false;
    auto *cmd = static_cast<const SetPositionCommand*>(other);
    return cmd->m_nodeId == m_nodeId;
}

bool SetPositionCommand::mergeWith(const ICommand *other)
{
    auto *cmd = static_cast<const SetPositionCommand*>(other);
    m_newPosition = cmd->m_newPosition;
    return true;
}

// ===== SetNodeNameCommand =====

SetNodeNameCommand::SetNodeNameCommand(SceneDocument *document, 
                                       SceneNode *node,
                                       const QString &newName)
    : SceneCommand(document)
    , m_newName(newName)
{
    if (node) {
        m_nodeId = node->uuid();
        m_oldName = node->name();
    }
}

bool SetNodeNameCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setName(m_newName);
    return true;
}

bool SetNodeNameCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setName(m_oldName);
    return true;
}

QString SetNodeNameCommand::description() const
{
    return QStringLiteral("Rename to %1").arg(m_newName);
}

// ===== SetNodeMaterialCommand =====

SetNodeMaterialCommand::SetNodeMaterialCommand(SceneDocument *document, 
                                               SceneNode *node,
                                               const QUuid &newMaterialId)
    : SceneCommand(document)
    , m_newMaterialId(newMaterialId)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        m_oldMaterialId = node->materialId();
    }
}

bool SetNodeMaterialCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setMaterialId(m_newMaterialId);
    return true;
}

bool SetNodeMaterialCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setMaterialId(m_oldMaterialId);
    return true;
}

QString SetNodeMaterialCommand::description() const
{
    return QStringLiteral("Change material of %1").arg(m_nodeName);
}

// ===== SetNodeVisibilityCommand =====

SetNodeVisibilityCommand::SetNodeVisibilityCommand(SceneDocument *document, 
                                                   SceneNode *node,
                                                   bool visible)
    : SceneCommand(document)
    , m_newVisible(visible)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        m_oldVisible = node->isVisible();
    }
}

bool SetNodeVisibilityCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setVisible(m_newVisible);
    return true;
}

bool SetNodeVisibilityCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setVisible(m_oldVisible);
    return true;
}

QString SetNodeVisibilityCommand::description() const
{
    return m_newVisible ? QStringLiteral("Show %1").arg(m_nodeName) 
                        : QStringLiteral("Hide %1").arg(m_nodeName);
}

// ===== SetGeometryParamsCommand =====

SetGeometryParamsCommand::SetGeometryParamsCommand(SceneDocument *document, 
                                                   SceneNode *node,
                                                   const GeometryParams &newParams)
    : SceneCommand(document)
    , m_newParams(newParams)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        m_oldParams = node->geometryParams();
    }
}

bool SetGeometryParamsCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setGeometryParams(m_newParams);
    return true;
}

bool SetGeometryParamsCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    if (!node) return false;
    
    node->setGeometryParams(m_oldParams);
    return true;
}

QString SetGeometryParamsCommand::description() const
{
    return QStringLiteral("Modify %1").arg(m_nodeName);
}

// ===== ReparentNodeCommand =====

ReparentNodeCommand::ReparentNodeCommand(SceneDocument *document, 
                                         SceneNode *node,
                                         SceneNode *newParent)
    : SceneCommand(document)
{
    if (node) {
        m_nodeId = node->uuid();
        m_nodeName = node->name();
        if (node->parentNode()) {
            m_oldParentId = node->parentNode()->uuid();
            m_oldIndex = node->parentNode()->indexOfChild(node);
        }
        m_newParentId = newParent ? newParent->uuid() : QUuid();
    }
}

bool ReparentNodeCommand::execute()
{
    SceneNode *node = document()->findNode(m_nodeId);
    SceneNode *newParent = m_newParentId.isNull() ? document()->rootNode() 
                                                   : document()->findNode(m_newParentId);
    if (!node) return false;
    
    document()->reparentNode(node, newParent);
    return true;
}

bool ReparentNodeCommand::undo()
{
    SceneNode *node = document()->findNode(m_nodeId);
    SceneNode *oldParent = m_oldParentId.isNull() ? document()->rootNode() 
                                                   : document()->findNode(m_oldParentId);
    if (!node) return false;
    
    document()->reparentNode(node, oldParent);
    return true;
}

QString ReparentNodeCommand::description() const
{
    return QStringLiteral("Reparent %1").arg(m_nodeName);
}

// ===== AddMaterialCommand =====

AddMaterialCommand::AddMaterialCommand(SceneDocument *document, 
                                       std::unique_ptr<MaterialDefinition> material)
    : SceneCommand(document)
    , m_material(std::move(material))
{
    if (m_material) {
        m_materialName = m_material->name();
    }
}

bool AddMaterialCommand::execute()
{
    if (!m_material) return false;
    
    m_materialPtr = document()->addMaterial(std::move(m_material));
    return m_materialPtr != nullptr;
}

bool AddMaterialCommand::undo()
{
    if (!m_materialPtr) return false;
    
    // Store material before removal
    auto &mats = const_cast<std::vector<std::unique_ptr<MaterialDefinition>>&>(document()->materials());
    for (auto it = mats.begin(); it != mats.end(); ++it) {
        if (it->get() == m_materialPtr) {
            m_material = std::move(*it);
            break;
        }
    }
    
    document()->removeMaterial(m_materialPtr->uuid());
    m_materialPtr = nullptr;
    return true;
}

QString AddMaterialCommand::description() const
{
    return QStringLiteral("Add material %1").arg(m_materialName);
}

// ===== DeleteMaterialCommand =====

DeleteMaterialCommand::DeleteMaterialCommand(SceneDocument *document, const QUuid &materialId)
    : SceneCommand(document)
    , m_materialId(materialId)
{
    if (auto *mat = document->findMaterial(materialId)) {
        m_materialName = mat->name();
    }
}

bool DeleteMaterialCommand::execute()
{
    auto *mat = document()->findMaterial(m_materialId);
    if (!mat) return false;
    
    // Store material before removal
    auto &mats = const_cast<std::vector<std::unique_ptr<MaterialDefinition>>&>(document()->materials());
    for (auto it = mats.begin(); it != mats.end(); ++it) {
        if ((*it)->uuid() == m_materialId) {
            m_material = std::move(*it);
            break;
        }
    }
    
    document()->removeMaterial(m_materialId);
    return true;
}

bool DeleteMaterialCommand::undo()
{
    if (!m_material) return false;
    
    document()->addMaterial(std::move(m_material));
    return true;
}

QString DeleteMaterialCommand::description() const
{
    return QStringLiteral("Delete material %1").arg(m_materialName);
}

// ===== SetMaterialColorCommand =====

SetMaterialColorCommand::SetMaterialColorCommand(SceneDocument *document, 
                                                 const QUuid &materialId,
                                                 const glm::vec3 &newColor)
    : SceneCommand(document)
    , m_materialId(materialId)
    , m_newColor(newColor)
{
    if (auto *mat = document->findMaterial(materialId)) {
        m_materialName = mat->name();
        m_oldColor = mat->color();
    }
}

bool SetMaterialColorCommand::execute()
{
    auto *mat = document()->findMaterial(m_materialId);
    if (!mat) return false;
    
    mat->setColor(m_newColor);
    return true;
}

bool SetMaterialColorCommand::undo()
{
    auto *mat = document()->findMaterial(m_materialId);
    if (!mat) return false;
    
    mat->setColor(m_oldColor);
    return true;
}

QString SetMaterialColorCommand::description() const
{
    return QStringLiteral("Change color of %1").arg(m_materialName);
}

bool SetMaterialColorCommand::canMergeWith(const ICommand *other) const
{
    if (other->typeId() != typeId()) return false;
    auto *cmd = static_cast<const SetMaterialColorCommand*>(other);
    return cmd->m_materialId == m_materialId;
}

bool SetMaterialColorCommand::mergeWith(const ICommand *other)
{
    auto *cmd = static_cast<const SetMaterialColorCommand*>(other);
    m_newColor = cmd->m_newColor;
    return true;
}

} // namespace scene
} // namespace Raytracer
