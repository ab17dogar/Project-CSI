#ifndef RAYTRACER_SELECTION_MANAGER_H
#define RAYTRACER_SELECTION_MANAGER_H

#include <QObject>
#include <QSet>
#include <QMap>
#include <QUuid>
#include <vector>

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneNode;

/**
 * @brief Manages selection state for scene objects
 * 
 * Handles single and multi-selection of scene nodes with
 * support for:
 * - Single selection
 * - Multi-selection (Ctrl+click)
 * - Range selection (Shift+click)
 * - Selection sets/groups
 * - Primary/secondary selection for multi-select operations
 */
class SelectionManager : public QObject {
    Q_OBJECT
    
public:
    explicit SelectionManager(QObject *parent = nullptr);
    ~SelectionManager() override = default;
    
    // Document binding
    void setSceneDocument(SceneDocument *document);
    SceneDocument* sceneDocument() const { return m_document; }
    
    // Selection queries
    bool isEmpty() const { return m_selection.isEmpty(); }
    int count() const { return m_selection.size(); }
    bool isSelected(const SceneNode *node) const;
    bool isSelected(const QUuid &nodeId) const;
    
    // Get selection
    QSet<QUuid> selectedIds() const { return m_selection; }
    std::vector<SceneNode*> selectedNodes() const;
    SceneNode* primarySelection() const;
    
    // Selection modification
    void select(SceneNode *node);
    void select(const QUuid &nodeId);
    void addToSelection(SceneNode *node);
    void addToSelection(const QUuid &nodeId);
    void removeFromSelection(SceneNode *node);
    void removeFromSelection(const QUuid &nodeId);
    void toggleSelection(SceneNode *node);
    void toggleSelection(const QUuid &nodeId);
    void selectAll();
    void clearSelection();
    
    // Batch operations
    void selectMultiple(const std::vector<SceneNode*> &nodes);
    void selectMultiple(const QSet<QUuid> &nodeIds);
    void setSelection(const std::vector<SceneNode*> &nodes);
    void setSelection(const QSet<QUuid> &nodeIds);
    
    // Selection operations
    void selectParent();
    void selectChildren();
    void selectSiblings();
    void invertSelection();
    void selectByType(int geometryType);
    void selectByMaterial(const QUuid &materialId);
    
    // Clipboard-style operations
    void storeSelection(const QString &name);
    void restoreSelection(const QString &name);
    QStringList storedSelectionNames() const;
    
signals:
    void selectionChanged();
    void primarySelectionChanged(SceneNode *node);
    void selectionCleared();
    
private:
    void emitSelectionChanged();
    void cleanupInvalidSelections();
    
private:
    SceneDocument *m_document = nullptr;
    QSet<QUuid> m_selection;
    QUuid m_primarySelectionId;  // First/main selected item
    
    // Stored selection sets
    QMap<QString, QSet<QUuid>> m_storedSelections;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SELECTION_MANAGER_H
