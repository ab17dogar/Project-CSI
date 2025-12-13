#pragma once

#include "Command.h"
#include "../models/SceneNode.h"
#include "../models/MaterialDefinition.h"
#include "../models/Transform.h"
#include <QUuid>
#include <glm/glm.hpp>
#include <memory>

namespace Raytracer {
namespace scene {

class SceneDocument;

/**
 * @brief Command type IDs for merge checking
 */
enum class CommandTypeId
{
    Generic = 0,
    AddNode = 1,
    DeleteNode = 2,
    Transform = 3,
    SetNodeProperty = 4,
    SetMaterialProperty = 5,
    Reparent = 6
};

// ===== Add Node Command =====

class AddNodeCommand : public SceneCommand
{
public:
    AddNodeCommand(SceneDocument *document, 
                   std::unique_ptr<SceneNode> node,
                   SceneNode *parent = nullptr);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::AddNode); }
    
private:
    std::unique_ptr<SceneNode> m_node;      // Owned when not in scene
    SceneNode *m_nodePtr {nullptr};          // Raw pointer when in scene
    QUuid m_parentId;
    QString m_nodeName;
};

// ===== Delete Node Command =====

class DeleteNodeCommand : public SceneCommand
{
public:
    DeleteNodeCommand(SceneDocument *document, SceneNode *node);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::DeleteNode); }
    
private:
    std::unique_ptr<SceneNode> m_node;      // Owned when removed from scene
    SceneNode *m_nodePtr {nullptr};
    QUuid m_nodeId;
    QUuid m_parentId;
    QString m_nodeName;
    int m_indexInParent {-1};
};

// ===== Transform Command =====

class TransformCommand : public SceneCommand
{
public:
    TransformCommand(SceneDocument *document, 
                     SceneNode *node,
                     const glm::vec3 &newPosition,
                     const glm::vec3 &newRotation,
                     const glm::vec3 &newScale);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::Transform); }
    
    bool canMergeWith(const ICommand *other) const override;
    bool mergeWith(const ICommand *other) override;
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    
    glm::vec3 m_oldPosition;
    glm::vec3 m_oldRotation;
    glm::vec3 m_oldScale;
    
    glm::vec3 m_newPosition;
    glm::vec3 m_newRotation;
    glm::vec3 m_newScale;
};

// ===== Set Position Command =====

class SetPositionCommand : public SceneCommand
{
public:
    SetPositionCommand(SceneDocument *document, 
                       SceneNode *node,
                       const glm::vec3 &newPosition);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::Transform); }
    
    bool canMergeWith(const ICommand *other) const override;
    bool mergeWith(const ICommand *other) override;
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    glm::vec3 m_oldPosition;
    glm::vec3 m_newPosition;
};

// ===== Set Node Name Command =====

class SetNodeNameCommand : public SceneCommand
{
public:
    SetNodeNameCommand(SceneDocument *document, 
                       SceneNode *node,
                       const QString &newName);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::SetNodeProperty); }
    
private:
    QUuid m_nodeId;
    QString m_oldName;
    QString m_newName;
};

// ===== Set Node Material Command =====

class SetNodeMaterialCommand : public SceneCommand
{
public:
    SetNodeMaterialCommand(SceneDocument *document, 
                           SceneNode *node,
                           const QUuid &newMaterialId);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::SetNodeProperty); }
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    QUuid m_oldMaterialId;
    QUuid m_newMaterialId;
};

// ===== Set Node Visibility Command =====

class SetNodeVisibilityCommand : public SceneCommand
{
public:
    SetNodeVisibilityCommand(SceneDocument *document, 
                             SceneNode *node,
                             bool visible);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::SetNodeProperty); }
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    bool m_oldVisible;
    bool m_newVisible;
};

// ===== Set Geometry Params Command =====

class SetGeometryParamsCommand : public SceneCommand
{
public:
    SetGeometryParamsCommand(SceneDocument *document, 
                             SceneNode *node,
                             const GeometryParams &newParams);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::SetNodeProperty); }
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    GeometryParams m_oldParams;
    GeometryParams m_newParams;
};

// ===== Reparent Node Command =====

class ReparentNodeCommand : public SceneCommand
{
public:
    ReparentNodeCommand(SceneDocument *document, 
                        SceneNode *node,
                        SceneNode *newParent);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::Reparent); }
    
private:
    QUuid m_nodeId;
    QString m_nodeName;
    QUuid m_oldParentId;
    QUuid m_newParentId;
    int m_oldIndex {-1};
};

// ===== Add Material Command =====

class AddMaterialCommand : public SceneCommand
{
public:
    AddMaterialCommand(SceneDocument *document, 
                       std::unique_ptr<MaterialDefinition> material);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    
private:
    std::unique_ptr<MaterialDefinition> m_material;
    MaterialDefinition *m_materialPtr {nullptr};
    QString m_materialName;
};

// ===== Delete Material Command =====

class DeleteMaterialCommand : public SceneCommand
{
public:
    DeleteMaterialCommand(SceneDocument *document, const QUuid &materialId);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    
private:
    std::unique_ptr<MaterialDefinition> m_material;
    QUuid m_materialId;
    QString m_materialName;
};

// ===== Set Material Property Command =====

class SetMaterialColorCommand : public SceneCommand
{
public:
    SetMaterialColorCommand(SceneDocument *document, 
                            const QUuid &materialId,
                            const glm::vec3 &newColor);
    
    bool execute() override;
    bool undo() override;
    QString description() const override;
    int typeId() const override { return static_cast<int>(CommandTypeId::SetMaterialProperty); }
    
    bool canMergeWith(const ICommand *other) const override;
    bool mergeWith(const ICommand *other) override;
    
private:
    QUuid m_materialId;
    QString m_materialName;
    glm::vec3 m_oldColor;
    glm::vec3 m_newColor;
};

} // namespace scene
} // namespace Raytracer
