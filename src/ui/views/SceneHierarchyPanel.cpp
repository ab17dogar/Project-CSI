#include "SceneHierarchyPanel.h"
#include "../commands/CommandHistory.h"
#include "../commands/SceneCommands.h"
#include "../controllers/SelectionManager.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"

#include <QDrag>
#include <QHeaderView>
#include <QIODevice>
#include <QMimeData>
#include <QVBoxLayout>

namespace Raytracer {
namespace scene {

// ===== SceneTreeModel =====

SceneTreeModel::SceneTreeModel(QObject *parent) : QAbstractItemModel(parent) {}

void SceneTreeModel::setSceneDocument(SceneDocument *document) {
  beginResetModel();

  if (m_document) {
    disconnect(m_document, nullptr, this, nullptr);
  }

  m_document = document;

  if (m_document) {
    connect(m_document, &SceneDocument::documentChanged, this,
            &SceneTreeModel::onSceneChanged);
    connect(m_document, &SceneDocument::nodeAdded, this,
            [this](SceneNode *node) {
              onNodeAdded(node ? node->uuid() : QUuid());
            });
    connect(m_document, &SceneDocument::nodeRemoved, this,
            &SceneTreeModel::onNodeRemoved);
    connect(m_document, &SceneDocument::nodeChanged, this,
            [this](SceneNode *node) {
              onNodeChanged(node ? node->uuid() : QUuid());
            });
  }

  endResetModel();
}

QModelIndex SceneTreeModel::index(int row, int column,
                                  const QModelIndex &parent) const {
  if (!m_document || column != 0)
    return QModelIndex();

  SceneNode *parentNode =
      parent.isValid() ? static_cast<SceneNode *>(parent.internalPointer())
                       : m_document->rootNode();

  if (!parentNode)
    return QModelIndex();

  const auto &children = parentNode->children();
  if (row < 0 || row >= static_cast<int>(children.size())) {
    return QModelIndex();
  }

  return createIndex(row, column, children[row]);
}

QModelIndex SceneTreeModel::parent(const QModelIndex &index) const {
  if (!index.isValid() || !m_document)
    return QModelIndex();

  SceneNode *node = static_cast<SceneNode *>(index.internalPointer());
  if (!node)
    return QModelIndex();

  SceneNode *parent = node->parentNode();
  if (!parent || parent == m_document->rootNode()) {
    return QModelIndex();
  }

  // Find parent's row in grandparent
  SceneNode *grandparent = parent->parentNode();
  if (!grandparent)
    grandparent = m_document->rootNode();

  int row = grandparent->indexOfChild(parent);
  return createIndex(row, 0, parent);
}

int SceneTreeModel::rowCount(const QModelIndex &parent) const {
  if (!m_document)
    return 0;

  SceneNode *node = parent.isValid()
                        ? static_cast<SceneNode *>(parent.internalPointer())
                        : m_document->rootNode();

  return node ? static_cast<int>(node->children().size()) : 0;
}

int SceneTreeModel::columnCount(const QModelIndex &parent) const {
  Q_UNUSED(parent);
  return 1;
}

QVariant SceneTreeModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid())
    return QVariant();

  SceneNode *node = static_cast<SceneNode *>(index.internalPointer());
  if (!node)
    return QVariant();

  switch (role) {
  case Qt::DisplayRole:
  case Qt::EditRole:
    return node->name();

  case Qt::DecorationRole: {
    // Return icon based on geometry type
    switch (node->geometryType()) {
    case GeometryType::Sphere:
      return QIcon::fromTheme("draw-circle");
    case GeometryType::Triangle:
      return QIcon::fromTheme("draw-triangle");
    case GeometryType::Mesh:
      return QIcon::fromTheme("draw-polygon");
    case GeometryType::None:
      return QIcon::fromTheme("folder");
    default:
      return QVariant();
    }
  }

  case Qt::ToolTipRole: {
    QString tip = node->name();
    switch (node->geometryType()) {
    case GeometryType::Sphere:
      tip += " (Sphere)";
      break;
    case GeometryType::Triangle:
      tip += " (Triangle)";
      break;
    case GeometryType::Mesh:
      tip += " (Mesh)";
      break;
    case GeometryType::None:
      tip += " (Group)";
      break;
    default:
      break;
    }
    return tip;
  }

  default:
    return QVariant();
  }
}

bool SceneTreeModel::setData(const QModelIndex &index, const QVariant &value,
                             int role) {
  if (!index.isValid() || role != Qt::EditRole)
    return false;

  SceneNode *node = static_cast<SceneNode *>(index.internalPointer());
  if (!node)
    return false;

  QString newName = value.toString().trimmed();
  if (newName.isEmpty() || newName == node->name())
    return false;

  node->setName(newName);
  emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
  return true;
}

Qt::ItemFlags SceneTreeModel::flags(const QModelIndex &index) const {
  Qt::ItemFlags flags = QAbstractItemModel::flags(index);

  if (index.isValid()) {
    flags |= Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;
  } else {
    flags |= Qt::ItemIsDropEnabled;
  }

  return flags;
}

QVariant SceneTreeModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (orientation == Qt::Horizontal && role == Qt::DisplayRole &&
      section == 0) {
    return QStringLiteral("Scene");
  }
  return QVariant();
}

Qt::DropActions SceneTreeModel::supportedDropActions() const {
  return Qt::MoveAction;
}

QStringList SceneTreeModel::mimeTypes() const {
  return {QStringLiteral("application/x-raytracer-scenenode")};
}

QMimeData *SceneTreeModel::mimeData(const QModelIndexList &indexes) const {
  QMimeData *mimeData = new QMimeData();
  QByteArray encodedData;
  QDataStream stream(&encodedData, QIODevice::WriteOnly);

  for (const QModelIndex &index : indexes) {
    if (index.isValid()) {
      SceneNode *node = static_cast<SceneNode *>(index.internalPointer());
      if (node) {
        stream << node->uuid().toString();
      }
    }
  }

  mimeData->setData(QStringLiteral("application/x-raytracer-scenenode"),
                    encodedData);
  return mimeData;
}

bool SceneTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                  int row, int column,
                                  const QModelIndex &parent) {
  Q_UNUSED(row);
  Q_UNUSED(column);

  if (action == Qt::IgnoreAction)
    return true;
  if (!data->hasFormat(QStringLiteral("application/x-raytracer-scenenode")))
    return false;
  if (!m_document)
    return false;

  QByteArray encodedData =
      data->data(QStringLiteral("application/x-raytracer-scenenode"));
  QDataStream stream(&encodedData, QIODevice::ReadOnly);

  SceneNode *newParent =
      parent.isValid() ? static_cast<SceneNode *>(parent.internalPointer())
                       : m_document->rootNode();

  while (!stream.atEnd()) {
    QString uuidStr;
    stream >> uuidStr;
    QUuid uuid(uuidStr);

    SceneNode *node = m_document->findNode(uuid);
    if (node && node != newParent) {
      // Don't allow dropping onto descendants
      SceneNode *check = newParent;
      bool isDescendant = false;
      while (check) {
        if (check == node) {
          isDescendant = true;
          break;
        }
        check = check->parentNode();
      }

      if (!isDescendant) {
        m_document->reparentNode(node, newParent);
      }
    }
  }

  return true;
}

QModelIndex SceneTreeModel::indexForNode(const SceneNode *node) const {
  if (!node || !m_document || node == m_document->rootNode()) {
    return QModelIndex();
  }

  SceneNode *parent = node->parentNode();
  if (!parent)
    parent = m_document->rootNode();

  int row = parent->indexOfChild(const_cast<SceneNode *>(node));
  if (row < 0)
    return QModelIndex();

  return createIndex(row, 0, const_cast<SceneNode *>(node));
}

SceneNode *SceneTreeModel::nodeForIndex(const QModelIndex &index) const {
  if (!index.isValid())
    return nullptr;
  return static_cast<SceneNode *>(index.internalPointer());
}

void SceneTreeModel::refresh() {
  beginResetModel();
  endResetModel();
}

void SceneTreeModel::onSceneChanged() {
  beginResetModel();
  endResetModel();
}

void SceneTreeModel::onNodeAdded(const QUuid &nodeId) {
  Q_UNUSED(nodeId);
  // For simplicity, refresh entire model
  // Could be optimized to only insert specific rows
  beginResetModel();
  endResetModel();
}

void SceneTreeModel::onNodeRemoved(const QUuid &nodeId) {
  Q_UNUSED(nodeId);
  beginResetModel();
  endResetModel();
}

void SceneTreeModel::onNodeChanged(const QUuid &nodeId) {
  if (!m_document)
    return;

  SceneNode *node = m_document->findNode(nodeId);
  if (node) {
    QModelIndex idx = indexForNode(node);
    if (idx.isValid()) {
      emit dataChanged(idx, idx);
    }
  }
}

// ===== SceneHierarchyPanel =====

SceneHierarchyPanel::SceneHierarchyPanel(QWidget *parent) : QWidget(parent) {
  setupUI();
  createActions();
  setupContextMenu();
}

void SceneHierarchyPanel::setupUI() {
  QVBoxLayout *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_model = new SceneTreeModel(this);

  m_treeView = new QTreeView(this);
  m_treeView->setModel(m_model);
  m_treeView->setHeaderHidden(true);
  m_treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_treeView->setDragEnabled(true);
  m_treeView->setAcceptDrops(true);
  m_treeView->setDropIndicatorShown(true);
  m_treeView->setDragDropMode(QAbstractItemView::InternalMove);
  m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
  m_treeView->setEditTriggers(QAbstractItemView::SelectedClicked |
                              QAbstractItemView::EditKeyPressed);

  connect(m_treeView, &QTreeView::doubleClicked, this,
          &SceneHierarchyPanel::onItemDoubleClicked);
  connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, &SceneHierarchyPanel::onSelectionChanged);
  connect(m_treeView, &QTreeView::customContextMenuRequested, this,
          &SceneHierarchyPanel::onCustomContextMenu);

  layout->addWidget(m_treeView);
}

void SceneHierarchyPanel::createActions() {
  m_addSphereAction =
      new QAction(QIcon::fromTheme("draw-circle"), tr("Add Sphere"), this);
  connect(m_addSphereAction, &QAction::triggered, this,
          &SceneHierarchyPanel::addSphere);

  m_addTriangleAction =
      new QAction(QIcon::fromTheme("draw-triangle"), tr("Add Triangle"), this);
  connect(m_addTriangleAction, &QAction::triggered, this,
          &SceneHierarchyPanel::addTriangle);

  m_addMeshAction =
      new QAction(QIcon::fromTheme("draw-polygon"), tr("Add Mesh"), this);
  connect(m_addMeshAction, &QAction::triggered, this,
          &SceneHierarchyPanel::addMesh);

  m_addGroupAction =
      new QAction(QIcon::fromTheme("folder"), tr("Add Group"), this);
  connect(m_addGroupAction, &QAction::triggered, this,
          &SceneHierarchyPanel::addGroup);

  m_deleteAction =
      new QAction(QIcon::fromTheme("edit-delete"), tr("Delete"), this);
  m_deleteAction->setShortcut(QKeySequence::Delete);
  connect(m_deleteAction, &QAction::triggered, this,
          &SceneHierarchyPanel::deleteSelected);

  m_duplicateAction =
      new QAction(QIcon::fromTheme("edit-copy"), tr("Duplicate"), this);
  m_duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
  connect(m_duplicateAction, &QAction::triggered, this,
          &SceneHierarchyPanel::duplicateSelected);

  m_renameAction = new QAction(tr("Rename"), this);
  m_renameAction->setShortcut(Qt::Key_F2);
  connect(m_renameAction, &QAction::triggered, this,
          &SceneHierarchyPanel::renameSelected);

  m_toggleVisibilityAction = new QAction(tr("Toggle Visibility"), this);
  m_toggleVisibilityAction->setShortcut(Qt::Key_H);
  connect(m_toggleVisibilityAction, &QAction::triggered, this,
          &SceneHierarchyPanel::toggleVisibilitySelected);
}

void SceneHierarchyPanel::setupContextMenu() {
  m_contextMenu = new QMenu(this);

  QMenu *addMenu = m_contextMenu->addMenu(tr("Add"));
  addMenu->addAction(m_addSphereAction);
  addMenu->addAction(m_addTriangleAction);
  addMenu->addAction(m_addMeshAction);
  addMenu->addSeparator();
  addMenu->addAction(m_addGroupAction);

  m_contextMenu->addSeparator();
  m_contextMenu->addAction(m_duplicateAction);
  m_contextMenu->addAction(m_renameAction);
  m_contextMenu->addAction(m_toggleVisibilityAction);
  m_contextMenu->addSeparator();
  m_contextMenu->addAction(m_deleteAction);
}

void SceneHierarchyPanel::setSceneDocument(SceneDocument *document) {
  m_model->setSceneDocument(document);
}

void SceneHierarchyPanel::setSelectionManager(SelectionManager *manager) {
  if (m_selectionManager) {
    disconnect(m_selectionManager, nullptr, this, nullptr);
  }

  m_selectionManager = manager;

  if (m_selectionManager) {
    connect(m_selectionManager, &SelectionManager::selectionChanged, this,
            &SceneHierarchyPanel::syncSelectionFromManager);
  }
}

void SceneHierarchyPanel::setCommandHistory(CommandHistory *history) {
  m_commandHistory = history;
}

SceneDocument *SceneHierarchyPanel::sceneDocument() const {
  return m_model->sceneDocument();
}

void SceneHierarchyPanel::syncSelectionFromManager() {
  if (m_updatingSelection || !m_selectionManager)
    return;

  m_updatingSelection = true;

  QItemSelection selection;
  for (const QUuid &id : m_selectionManager->selectedIds()) {
    if (SceneNode *node = m_model->sceneDocument()->findNode(id)) {
      QModelIndex idx = m_model->indexForNode(node);
      if (idx.isValid()) {
        selection.select(idx, idx);
      }
    }
  }

  m_treeView->selectionModel()->select(selection,
                                       QItemSelectionModel::ClearAndSelect |
                                           QItemSelectionModel::Rows);

  m_updatingSelection = false;
}

void SceneHierarchyPanel::syncSelectionToManager() {
  if (m_updatingSelection || !m_selectionManager)
    return;

  m_updatingSelection = true;

  QSet<QUuid> selectedIds;
  for (const QModelIndex &idx :
       m_treeView->selectionModel()->selectedIndexes()) {
    if (SceneNode *node = m_model->nodeForIndex(idx)) {
      selectedIds.insert(node->uuid());
    }
  }

  m_selectionManager->setSelection(selectedIds);

  m_updatingSelection = false;
}

void SceneHierarchyPanel::expandToNode(SceneNode *node) {
  if (!node)
    return;

  // Expand all ancestors
  SceneNode *parent = node->parentNode();
  while (parent && parent != m_model->sceneDocument()->rootNode()) {
    QModelIndex idx = m_model->indexForNode(parent);
    if (idx.isValid()) {
      m_treeView->expand(idx);
    }
    parent = parent->parentNode();
  }

  // Scroll to and select the node
  QModelIndex idx = m_model->indexForNode(node);
  if (idx.isValid()) {
    m_treeView->scrollTo(idx);
    m_treeView->selectionModel()->select(
        idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  }
}

void SceneHierarchyPanel::refresh() { m_model->refresh(); }

void SceneHierarchyPanel::onItemDoubleClicked(const QModelIndex &index) {
  if (SceneNode *node = m_model->nodeForIndex(index)) {
    emit nodeDoubleClicked(node);
  }
}

void SceneHierarchyPanel::onSelectionChanged(const QItemSelection &selected,
                                             const QItemSelection &deselected) {
  Q_UNUSED(selected);
  Q_UNUSED(deselected);
  syncSelectionToManager();
}

void SceneHierarchyPanel::onCustomContextMenu(const QPoint &pos) {
  QModelIndex index = m_treeView->indexAt(pos);
  SceneNode *node = m_model->nodeForIndex(index);

  // Enable/disable actions based on selection
  bool hasSelection =
      !m_treeView->selectionModel()->selectedIndexes().isEmpty();
  m_deleteAction->setEnabled(hasSelection);
  m_duplicateAction->setEnabled(hasSelection);
  m_renameAction->setEnabled(hasSelection);
  m_toggleVisibilityAction->setEnabled(hasSelection);

  emit contextMenuRequested(node, m_treeView->mapToGlobal(pos));
  m_contextMenu->exec(m_treeView->mapToGlobal(pos));
}

void SceneHierarchyPanel::addSphere() {
  auto *doc = sceneDocument();
  if (!doc)
    return;

  SceneNode *parent = nullptr;
  if (m_selectionManager && m_selectionManager->primarySelection()) {
    parent = m_selectionManager->primarySelection();
  }

  auto node = SceneNode::createSphere(QStringLiteral("Sphere"));

  if (m_commandHistory) {
    auto cmd = std::make_unique<AddNodeCommand>(doc, std::move(node), parent);
    m_commandHistory->execute(std::move(cmd));
  } else {
    doc->addNode(std::move(node), parent);
  }
}

void SceneHierarchyPanel::addTriangle() {
  auto *doc = sceneDocument();
  if (!doc)
    return;

  SceneNode *parent = nullptr;
  if (m_selectionManager && m_selectionManager->primarySelection()) {
    parent = m_selectionManager->primarySelection();
  }

  auto node = SceneNode::createTriangle(QStringLiteral("Triangle"));

  if (m_commandHistory) {
    auto cmd = std::make_unique<AddNodeCommand>(doc, std::move(node), parent);
    m_commandHistory->execute(std::move(cmd));
  } else {
    doc->addNode(std::move(node), parent);
  }
}

void SceneHierarchyPanel::addMesh() {
  auto *doc = sceneDocument();
  if (!doc)
    return;

  SceneNode *parent = nullptr;
  if (m_selectionManager && m_selectionManager->primarySelection()) {
    parent = m_selectionManager->primarySelection();
  }

  auto node = SceneNode::createMesh(QStringLiteral("Mesh"));

  if (m_commandHistory) {
    auto cmd = std::make_unique<AddNodeCommand>(doc, std::move(node), parent);
    m_commandHistory->execute(std::move(cmd));
  } else {
    doc->addNode(std::move(node), parent);
  }
}

void SceneHierarchyPanel::addGroup() {
  auto *doc = sceneDocument();
  if (!doc)
    return;

  SceneNode *parent = nullptr;
  if (m_selectionManager && m_selectionManager->primarySelection()) {
    parent = m_selectionManager->primarySelection();
  }

  auto node = SceneNode::createGroup(QStringLiteral("Group"));

  if (m_commandHistory) {
    auto cmd = std::make_unique<AddNodeCommand>(doc, std::move(node), parent);
    m_commandHistory->execute(std::move(cmd));
  } else {
    doc->addNode(std::move(node), parent);
  }
}

void SceneHierarchyPanel::deleteSelected() {
  auto *doc = sceneDocument();
  if (!doc || !m_selectionManager)
    return;

  auto selected = m_selectionManager->selectedNodes();
  if (selected.empty())
    return;

  if (m_commandHistory && selected.size() > 1) {
    m_commandHistory->beginMacro(
        QStringLiteral("Delete %1 objects").arg(selected.size()));
  }

  for (SceneNode *node : selected) {
    if (m_commandHistory) {
      auto cmd = std::make_unique<DeleteNodeCommand>(doc, node);
      m_commandHistory->execute(std::move(cmd));
    } else {
      doc->removeNode(node);
    }
  }

  if (m_commandHistory && selected.size() > 1) {
    m_commandHistory->endMacro();
  }

  m_selectionManager->clearSelection();
}

void SceneHierarchyPanel::duplicateSelected() {
  auto *doc = sceneDocument();
  if (!doc || !m_selectionManager)
    return;

  auto selected = m_selectionManager->selectedNodes();
  if (selected.empty())
    return;

  // TODO: Implement node duplication
  // Would need a clone() method on SceneNode
}

void SceneHierarchyPanel::renameSelected() {
  QModelIndexList selected = m_treeView->selectionModel()->selectedIndexes();
  if (!selected.isEmpty()) {
    m_treeView->edit(selected.first());
  }
}

void SceneHierarchyPanel::toggleVisibilitySelected() {
  if (!m_selectionManager)
    return;

  auto selected = m_selectionManager->selectedNodes();
  for (SceneNode *node : selected) {
    if (node) {
      node->setVisible(!node->isVisible());
    }
  }
}

} // namespace scene
} // namespace Raytracer
