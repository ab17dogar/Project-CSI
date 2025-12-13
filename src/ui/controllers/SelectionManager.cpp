#include "SelectionManager.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"

namespace Raytracer {
namespace scene {

SelectionManager::SelectionManager(QObject *parent)
    : QObject(parent)
{
}

void SelectionManager::setSceneDocument(SceneDocument *document)
{
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    
    m_document = document;
    clearSelection();
    
    if (m_document) {
        connect(m_document, &SceneDocument::nodeRemoved, this, [this](const QUuid &id) {
            removeFromSelection(id);
        });
    }
}

bool SelectionManager::isSelected(const SceneNode *node) const
{
    return node && m_selection.contains(node->uuid());
}

bool SelectionManager::isSelected(const QUuid &nodeId) const
{
    return m_selection.contains(nodeId);
}

std::vector<SceneNode*> SelectionManager::selectedNodes() const
{
    std::vector<SceneNode*> nodes;
    if (!m_document) return nodes;
    
    for (const QUuid &id : m_selection) {
        if (SceneNode *node = m_document->findNode(id)) {
            nodes.push_back(node);
        }
    }
    return nodes;
}

SceneNode* SelectionManager::primarySelection() const
{
    if (!m_document || m_primarySelectionId.isNull()) return nullptr;
    return m_document->findNode(m_primarySelectionId);
}

void SelectionManager::select(SceneNode *node)
{
    if (!node) {
        clearSelection();
        return;
    }
    
    m_selection.clear();
    m_selection.insert(node->uuid());
    m_primarySelectionId = node->uuid();
    emitSelectionChanged();
}

void SelectionManager::select(const QUuid &nodeId)
{
    if (nodeId.isNull()) {
        clearSelection();
        return;
    }
    
    m_selection.clear();
    m_selection.insert(nodeId);
    m_primarySelectionId = nodeId;
    emitSelectionChanged();
}

void SelectionManager::addToSelection(SceneNode *node)
{
    if (!node) return;
    addToSelection(node->uuid());
}

void SelectionManager::addToSelection(const QUuid &nodeId)
{
    if (nodeId.isNull() || m_selection.contains(nodeId)) return;
    
    m_selection.insert(nodeId);
    if (m_primarySelectionId.isNull()) {
        m_primarySelectionId = nodeId;
    }
    emitSelectionChanged();
}

void SelectionManager::removeFromSelection(SceneNode *node)
{
    if (!node) return;
    removeFromSelection(node->uuid());
}

void SelectionManager::removeFromSelection(const QUuid &nodeId)
{
    if (!m_selection.contains(nodeId)) return;
    
    m_selection.remove(nodeId);
    
    // Update primary selection if removed
    if (m_primarySelectionId == nodeId) {
        m_primarySelectionId = m_selection.isEmpty() ? QUuid() : *m_selection.begin();
    }
    
    emitSelectionChanged();
}

void SelectionManager::toggleSelection(SceneNode *node)
{
    if (!node) return;
    toggleSelection(node->uuid());
}

void SelectionManager::toggleSelection(const QUuid &nodeId)
{
    if (m_selection.contains(nodeId)) {
        removeFromSelection(nodeId);
    } else {
        addToSelection(nodeId);
    }
}

void SelectionManager::selectAll()
{
    if (!m_document) return;
    
    m_selection.clear();
    m_primarySelectionId = QUuid();
    
    for (const auto &node : m_document->allNodes()) {
        m_selection.insert(node->uuid());
        if (m_primarySelectionId.isNull()) {
            m_primarySelectionId = node->uuid();
        }
    }
    
    emitSelectionChanged();
}

void SelectionManager::clearSelection()
{
    if (m_selection.isEmpty()) return;
    
    m_selection.clear();
    m_primarySelectionId = QUuid();
    emit selectionCleared();
    emit selectionChanged();
}

void SelectionManager::selectMultiple(const std::vector<SceneNode*> &nodes)
{
    for (SceneNode *node : nodes) {
        if (node) {
            m_selection.insert(node->uuid());
            if (m_primarySelectionId.isNull()) {
                m_primarySelectionId = node->uuid();
            }
        }
    }
    emitSelectionChanged();
}

void SelectionManager::selectMultiple(const QSet<QUuid> &nodeIds)
{
    for (const QUuid &id : nodeIds) {
        if (!id.isNull()) {
            m_selection.insert(id);
            if (m_primarySelectionId.isNull()) {
                m_primarySelectionId = id;
            }
        }
    }
    emitSelectionChanged();
}

void SelectionManager::setSelection(const std::vector<SceneNode*> &nodes)
{
    m_selection.clear();
    m_primarySelectionId = QUuid();
    selectMultiple(nodes);
}

void SelectionManager::setSelection(const QSet<QUuid> &nodeIds)
{
    m_selection.clear();
    m_primarySelectionId = QUuid();
    selectMultiple(nodeIds);
}

void SelectionManager::selectParent()
{
    if (!m_document || m_selection.isEmpty()) return;
    
    QSet<QUuid> newSelection;
    for (const QUuid &id : m_selection) {
        if (SceneNode *node = m_document->findNode(id)) {
            if (SceneNode *parent = node->parentNode()) {
                if (parent != m_document->rootNode()) {
                    newSelection.insert(parent->uuid());
                }
            }
        }
    }
    
    if (!newSelection.isEmpty()) {
        setSelection(newSelection);
    }
}

void SelectionManager::selectChildren()
{
    if (!m_document || m_selection.isEmpty()) return;
    
    QSet<QUuid> newSelection;
    for (const QUuid &id : m_selection) {
        if (SceneNode *node = m_document->findNode(id)) {
            for (SceneNode *child : node->children()) {
                newSelection.insert(child->uuid());
            }
        }
    }
    
    if (!newSelection.isEmpty()) {
        setSelection(newSelection);
    }
}

void SelectionManager::selectSiblings()
{
    if (!m_document || m_selection.isEmpty()) return;
    
    QSet<QUuid> newSelection;
    for (const QUuid &id : m_selection) {
        if (SceneNode *node = m_document->findNode(id)) {
            if (SceneNode *parent = node->parentNode()) {
                for (SceneNode *sibling : parent->children()) {
                    newSelection.insert(sibling->uuid());
                }
            }
        }
    }
    
    setSelection(newSelection);
}

void SelectionManager::invertSelection()
{
    if (!m_document) return;
    
    QSet<QUuid> newSelection;
    for (const auto &node : m_document->allNodes()) {
        if (!m_selection.contains(node->uuid())) {
            newSelection.insert(node->uuid());
        }
    }
    
    setSelection(newSelection);
}

void SelectionManager::selectByType(int geometryType)
{
    if (!m_document) return;
    
    QSet<QUuid> newSelection;
    for (const auto &node : m_document->allNodes()) {
        if (static_cast<int>(node->geometryType()) == geometryType) {
            newSelection.insert(node->uuid());
        }
    }
    
    setSelection(newSelection);
}

void SelectionManager::selectByMaterial(const QUuid &materialId)
{
    if (!m_document) return;
    
    QSet<QUuid> newSelection;
    for (const auto &node : m_document->allNodes()) {
        if (node->materialId() == materialId) {
            newSelection.insert(node->uuid());
        }
    }
    
    setSelection(newSelection);
}

void SelectionManager::storeSelection(const QString &name)
{
    m_storedSelections[name] = m_selection;
}

void SelectionManager::restoreSelection(const QString &name)
{
    if (m_storedSelections.contains(name)) {
        setSelection(m_storedSelections[name]);
    }
}

QStringList SelectionManager::storedSelectionNames() const
{
    return m_storedSelections.keys();
}

void SelectionManager::emitSelectionChanged()
{
    cleanupInvalidSelections();
    emit selectionChanged();
    emit primarySelectionChanged(primarySelection());
}

void SelectionManager::cleanupInvalidSelections()
{
    if (!m_document) {
        m_selection.clear();
        m_primarySelectionId = QUuid();
        return;
    }
    
    QSet<QUuid> toRemove;
    for (const QUuid &id : m_selection) {
        if (!m_document->findNode(id)) {
            toRemove.insert(id);
        }
    }
    
    for (const QUuid &id : toRemove) {
        m_selection.remove(id);
    }
    
    if (!m_primarySelectionId.isNull() && !m_selection.contains(m_primarySelectionId)) {
        m_primarySelectionId = m_selection.isEmpty() ? QUuid() : *m_selection.begin();
    }
}

} // namespace scene
} // namespace Raytracer
