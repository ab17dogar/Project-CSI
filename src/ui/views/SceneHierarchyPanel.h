#ifndef RAYTRACER_SCENE_HIERARCHY_PANEL_H
#define RAYTRACER_SCENE_HIERARCHY_PANEL_H

#include <QAbstractItemModel>
#include <QItemSelectionModel>
#include <QMenu>
#include <QTreeView>
#include <QWidget>

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneNode;
class SelectionManager;
class CommandHistory;

/**
 * @brief Qt item model for scene hierarchy tree view
 */
class SceneTreeModel : public QAbstractItemModel {
  Q_OBJECT

public:
  explicit SceneTreeModel(QObject *parent = nullptr);
  ~SceneTreeModel() override = default;

  void setSceneDocument(SceneDocument *document);
  SceneDocument *sceneDocument() const { return m_document; }

  // QAbstractItemModel interface
  QModelIndex index(int row, int column,
                    const QModelIndex &parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &index) const override;
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  bool setData(const QModelIndex &index, const QVariant &value,
               int role = Qt::EditRole) override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;

  // Drag and drop
  Qt::DropActions supportedDropActions() const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                    int column, const QModelIndex &parent) override;

  // Custom methods
  QModelIndex indexForNode(const SceneNode *node) const;
  SceneNode *nodeForIndex(const QModelIndex &index) const;
  void refresh();

public slots:
  void onSceneChanged();
  void onNodeAdded(const QUuid &nodeId);
  void onNodeRemoved(const QUuid &nodeId);
  void onNodeChanged(const QUuid &nodeId);

private:
  SceneDocument *m_document = nullptr;
};

/**
 * @brief Tree view panel showing scene hierarchy
 *
 * Provides a tree view of the scene graph with:
 * - Drag and drop for reparenting
 * - Context menu for operations
 * - Multi-selection support
 * - Inline renaming
 */
class SceneHierarchyPanel : public QWidget {
  Q_OBJECT

public:
  explicit SceneHierarchyPanel(QWidget *parent = nullptr);
  ~SceneHierarchyPanel() override = default;

  // Connections
  void setSceneDocument(SceneDocument *document);
  void setSelectionManager(SelectionManager *manager);
  void setCommandHistory(CommandHistory *history);

  SceneDocument *sceneDocument() const;
  SelectionManager *selectionManager() const { return m_selectionManager; }
  CommandHistory *commandHistory() const { return m_commandHistory; }

  // Access
  QTreeView *treeView() const { return m_treeView; }
  SceneTreeModel *model() const { return m_model; }

signals:
  void nodeDoubleClicked(SceneNode *node);
  void contextMenuRequested(SceneNode *node, const QPoint &globalPos);

public slots:
  void syncSelectionFromManager();
  void syncSelectionToManager();
  void expandToNode(SceneNode *node);
  void refresh();

private slots:
  void onItemDoubleClicked(const QModelIndex &index);
  void onSelectionChanged(const QItemSelection &selected,
                          const QItemSelection &deselected);
  void onCustomContextMenu(const QPoint &pos);

private:
  void setupUI();
  void setupContextMenu();
  void createActions();

  // Actions
  void addSphere();
  void addTriangle();
  void addMesh();
  void addGroup();
  void deleteSelected();
  void duplicateSelected();
  void renameSelected();
  void toggleVisibilitySelected();

private:
  QTreeView *m_treeView = nullptr;
  SceneTreeModel *m_model = nullptr;

  SelectionManager *m_selectionManager = nullptr;
  CommandHistory *m_commandHistory = nullptr;

  // Context menu
  QMenu *m_contextMenu = nullptr;
  QAction *m_addSphereAction = nullptr;
  QAction *m_addTriangleAction = nullptr;
  QAction *m_addMeshAction = nullptr;
  QAction *m_addGroupAction = nullptr;
  QAction *m_deleteAction = nullptr;
  QAction *m_duplicateAction = nullptr;
  QAction *m_renameAction = nullptr;
  QAction *m_toggleVisibilityAction = nullptr;

  // Internal state
  bool m_updatingSelection = false;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SCENE_HIERARCHY_PANEL_H
