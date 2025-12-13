#include "SceneEditorWidget.h"
#include "../commands/CommandHistory.h"
#include "../commands/SceneCommands.h"
#include "../controllers/SelectionManager.h"
#include "../models/DesignSpaceFactory.h"
#include "../models/MaterialDefinition.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"
#include "../serialization/SceneSerializer.h"
#include "../serialization/XMLSceneSerializer.h"
#include "PropertiesPanel.h"
#include "SceneHierarchyPanel.h"
#include "SceneViewport.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

namespace Raytracer {
namespace scene {

SceneEditorWidget::SceneEditorWidget(QWidget *parent)
    : QWidget(parent), m_document(std::make_unique<SceneDocument>()),
      m_selectionManager(std::make_unique<SelectionManager>()),
      m_commandHistory(std::make_unique<CommandHistory>()) {
  // Register serializers
  registerXMLSceneSerializer();

  setupUI();
  createActions();
  setupToolbar();
  setupConnections();

  // Initialize with new empty scene
  newScene();
}

SceneEditorWidget::~SceneEditorWidget() = default;

bool SceneEditorWidget::hasUnsavedChanges() const {
  return m_modified || !m_commandHistory->isClean();
}

void SceneEditorWidget::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);
  mainLayout->setSpacing(4);

  // Toolbar at top
  m_toolbar = new QToolBar(this);
  m_toolbar->setMovable(false);
  m_toolbar->setIconSize(QSize(20, 20));
  mainLayout->addWidget(m_toolbar);

  // Main splitter: left panel | viewport | right panel
  m_mainSplitter = new QSplitter(Qt::Horizontal, this);
  m_mainSplitter->setHandleWidth(4);
  m_mainSplitter->setChildrenCollapsible(false);

  // Left side: hierarchy panel
  m_hierarchyPanel = new SceneHierarchyPanel(m_mainSplitter);
  m_hierarchyPanel->setMinimumWidth(180);
  m_hierarchyPanel->setMaximumWidth(300);
  m_mainSplitter->addWidget(m_hierarchyPanel);

  // Center: 3D viewport wrapped in a container widget
  // This helps with OpenGL widget compositing issues
  QWidget *viewportContainer = new QWidget(m_mainSplitter);
  QVBoxLayout *viewportLayout = new QVBoxLayout(viewportContainer);
  viewportLayout->setContentsMargins(0, 0, 0, 0);
  viewportLayout->setSpacing(0);

  m_viewport = new SceneViewport(viewportContainer);
  m_viewport->setMinimumSize(300, 200);
  viewportLayout->addWidget(m_viewport);

  m_mainSplitter->addWidget(viewportContainer);

  // Right side: properties panel
  m_propertiesPanel = new PropertiesPanel(m_mainSplitter);
  m_propertiesPanel->setMinimumWidth(200);
  m_propertiesPanel->setMaximumWidth(350);
  m_mainSplitter->addWidget(m_propertiesPanel);

  // Set splitter proportions
  m_mainSplitter->setStretchFactor(0, 0); // Hierarchy - fixed
  m_mainSplitter->setStretchFactor(1, 1); // Viewport - stretchy
  m_mainSplitter->setStretchFactor(2, 0); // Properties - fixed
  m_mainSplitter->setSizes({200, 600, 250});

  mainLayout->addWidget(m_mainSplitter, 1);
}

void SceneEditorWidget::createActions() {
  // File actions
  m_newAction = new QAction(QIcon::fromTheme("document-new"), tr("&New"), this);
  m_newAction->setShortcut(QKeySequence::New);
  connect(m_newAction, &QAction::triggered, this, &SceneEditorWidget::newScene);

  m_openAction =
      new QAction(QIcon::fromTheme("document-open"), tr("&Open..."), this);
  m_openAction->setShortcut(QKeySequence::Open);
  connect(m_openAction, &QAction::triggered, this, [this]() { openScene(); });

  m_saveAction =
      new QAction(QIcon::fromTheme("document-save"), tr("&Save"), this);
  m_saveAction->setShortcut(QKeySequence::Save);
  connect(m_saveAction, &QAction::triggered, this,
          &SceneEditorWidget::saveScene);

  m_saveAsAction = new QAction(QIcon::fromTheme("document-save-as"),
                               tr("Save &As..."), this);
  m_saveAsAction->setShortcut(QKeySequence::SaveAs);
  connect(m_saveAsAction, &QAction::triggered, this,
          &SceneEditorWidget::saveSceneAs);

  // Edit actions
  m_undoAction = new QAction(QIcon::fromTheme("edit-undo"), tr("&Undo"), this);
  m_undoAction->setShortcut(QKeySequence::Undo);
  m_undoAction->setEnabled(false);
  connect(m_undoAction, &QAction::triggered, this, &SceneEditorWidget::undo);

  m_redoAction = new QAction(QIcon::fromTheme("edit-redo"), tr("&Redo"), this);
  m_redoAction->setShortcut(QKeySequence::Redo);
  m_redoAction->setEnabled(false);
  connect(m_redoAction, &QAction::triggered, this, &SceneEditorWidget::redo);

  m_deleteAction =
      new QAction(QIcon::fromTheme("edit-delete"), tr("&Delete"), this);
  m_deleteAction->setShortcut(QKeySequence::Delete);
  connect(m_deleteAction, &QAction::triggered, this,
          &SceneEditorWidget::deleteSelected);

  m_duplicateAction =
      new QAction(QIcon::fromTheme("edit-copy"), tr("D&uplicate"), this);
  m_duplicateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
  connect(m_duplicateAction, &QAction::triggered, this,
          &SceneEditorWidget::duplicateSelected);

  // Add object actions
  m_addSphereAction =
      new QAction(QIcon::fromTheme("draw-circle"), tr("Add &Sphere"), this);
  connect(m_addSphereAction, &QAction::triggered, this,
          &SceneEditorWidget::addSphere);

  m_addTriangleAction =
      new QAction(QIcon::fromTheme("draw-triangle"), tr("Add &Triangle"), this);
  connect(m_addTriangleAction, &QAction::triggered, this,
          &SceneEditorWidget::addTriangle);

  m_addMeshAction =
      new QAction(QIcon::fromTheme("draw-polygon"), tr("Add &Mesh"), this);
  connect(m_addMeshAction, &QAction::triggered, this,
          &SceneEditorWidget::addMesh);

  m_addGroupAction =
      new QAction(QIcon::fromTheme("folder"), tr("Add &Group"), this);
  connect(m_addGroupAction, &QAction::triggered, this,
          &SceneEditorWidget::addGroup);

  // View actions
  m_toggleGridAction = new QAction(tr("Show &Grid"), this);
  m_toggleGridAction->setCheckable(true);
  m_toggleGridAction->setChecked(true);
  connect(m_toggleGridAction, &QAction::triggered, this,
          &SceneEditorWidget::toggleGrid);

  m_toggleAxesAction = new QAction(tr("Show &Axes"), this);
  m_toggleAxesAction->setCheckable(true);
  m_toggleAxesAction->setChecked(true);
  connect(m_toggleAxesAction, &QAction::triggered, this,
          &SceneEditorWidget::toggleAxes);

  m_resetViewAction = new QAction(tr("&Reset View"), this);
  m_resetViewAction->setShortcut(Qt::Key_Home);
  connect(m_resetViewAction, &QAction::triggered, this,
          &SceneEditorWidget::resetView);

  m_focusAction = new QAction(tr("&Focus Selection"), this);
  m_focusAction->setShortcut(Qt::Key_F);
  connect(m_focusAction, &QAction::triggered, this,
          &SceneEditorWidget::focusOnSelection);

  // Render action
  m_renderAction = new QAction(QIcon::fromTheme("media-playback-start"),
                               tr("&Render"), this);
  m_renderAction->setShortcut(Qt::Key_F5);
  connect(m_renderAction, &QAction::triggered, this,
          &SceneEditorWidget::renderScene);
}

void SceneEditorWidget::setupToolbar() {
  m_toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);

  // File section
  m_toolbar->addAction(m_newAction);
  m_toolbar->addAction(m_openAction);
  m_toolbar->addAction(m_saveAction);
  m_toolbar->addSeparator();

  // Edit section
  m_toolbar->addAction(m_undoAction);
  m_toolbar->addAction(m_redoAction);
  m_toolbar->addSeparator();

  // Add objects section - use text since icons might not exist
  m_addSphereAction->setText(tr("⚪ Sphere"));
  m_addTriangleAction->setText(tr("△ Triangle"));
  m_addMeshAction->setText(tr("📦 Mesh"));
  m_addGroupAction->setText(tr("📁 Group"));

  m_toolbar->addAction(m_addSphereAction);
  m_toolbar->addAction(m_addTriangleAction);
  m_toolbar->addAction(m_addMeshAction);
  m_toolbar->addAction(m_addGroupAction);
  m_toolbar->addSeparator();

  // Delete/Duplicate
  m_deleteAction->setText(tr("🗑️ Delete"));
  m_duplicateAction->setText(tr("📋 Duplicate"));
  m_toolbar->addAction(m_deleteAction);
  m_toolbar->addAction(m_duplicateAction);
  m_toolbar->addSeparator();

  // Render
  m_renderAction->setText(tr("▶ Render"));
  m_toolbar->addAction(m_renderAction);
}

void SceneEditorWidget::setupConnections() {
  // Connect document signals
  connect(m_document.get(), &SceneDocument::documentChanged, this,
          &SceneEditorWidget::onDocumentModified);
  connect(m_document.get(), &SceneDocument::nodeAdded, this,
          [this](SceneNode *) { onDocumentModified(); });
  connect(m_document.get(), &SceneDocument::nodeRemoved, this,
          [this](const QUuid &) { onDocumentModified(); });
  connect(m_document.get(), &SceneDocument::nodeChanged, this,
          [this](SceneNode *) { onDocumentModified(); });

  // Connect command history signals
  connect(m_commandHistory.get(), &CommandHistory::canUndoChanged, this,
          &SceneEditorWidget::onUndoRedoChanged);
  connect(m_commandHistory.get(), &CommandHistory::canRedoChanged, this,
          &SceneEditorWidget::onUndoRedoChanged);

  // Connect selection manager
  m_selectionManager->setSceneDocument(m_document.get());

  // Connect panels to document and selection
  m_viewport->setSceneDocument(m_document.get());
  m_viewport->setSelectionManager(m_selectionManager.get());

  m_hierarchyPanel->setSceneDocument(m_document.get());
  m_hierarchyPanel->setSelectionManager(m_selectionManager.get());
  m_hierarchyPanel->setCommandHistory(m_commandHistory.get());

  m_propertiesPanel->setSceneDocument(m_document.get());
  m_propertiesPanel->setSelectionManager(m_selectionManager.get());
  m_propertiesPanel->setCommandHistory(m_commandHistory.get());
}

void SceneEditorWidget::setupMenus() {
  // Menus would typically be added to a parent window's menu bar
  // This widget exposes actions that can be added to menus
}

// ===== File Operations =====

void SceneEditorWidget::newScene() {
  if (!maybeSave())
    return;

  m_commandHistory->clear();
  m_selectionManager->clearSelection();

  // Use DesignSpaceFactory for scalable room preset
  DesignSpaceFactory::applyPreset(m_document.get(),
                                  DesignSpaceFactory::PresetType::IndoorRoom);

  setCurrentFile(QString());
  m_modified = false;

  emit documentChanged();
}

bool SceneEditorWidget::openScene(const QString &filePath) {
  if (!maybeSave())
    return false;

  QString path = filePath;
  if (path.isEmpty()) {
    path = QFileDialog::getOpenFileName(
        this, tr("Open Scene"), QString(),
        SceneSerializerFactory::instance().getAllFilters());
    if (path.isEmpty())
      return false;
  }

  ISceneSerializer *serializer =
      SceneSerializerFactory::instance().getSerializer(path);
  if (!serializer) {
    QMessageBox::warning(this, tr("Error"),
                         tr("No serializer found for file: %1").arg(path));
    return false;
  }

  SerializationResult result = serializer->load(path, m_document.get());
  if (!result.success) {
    QString msg = tr("Failed to load scene: %1").arg(result.errorMessage);
    if (result.errorLine >= 0) {
      msg += tr(" (line %1)").arg(result.errorLine);
    }
    QMessageBox::warning(this, tr("Error"), msg);
    return false;
  }

  m_commandHistory->clear();
  m_selectionManager->clearSelection();
  setCurrentFile(path);
  m_modified = false;

  emit documentChanged();
  return true;
}

bool SceneEditorWidget::saveScene() {
  if (m_currentFilePath.isEmpty()) {
    return saveSceneAs();
  }

  ISceneSerializer *serializer =
      SceneSerializerFactory::instance().getSerializer(m_currentFilePath);
  if (!serializer) {
    return saveSceneAs();
  }

  SerializationResult result =
      serializer->save(m_currentFilePath, m_document.get());
  if (!result.success) {
    QMessageBox::warning(
        this, tr("Error"),
        tr("Failed to save scene: %1").arg(result.errorMessage));
    return false;
  }

  m_commandHistory->setClean();
  m_modified = false;
  emit documentSaved();
  return true;
}

bool SceneEditorWidget::saveSceneAs() {
  // Default to assets directory
  QString assetsDir =
      QCoreApplication::applicationDirPath() + QStringLiteral("/../assets");
  QDir dir(assetsDir);
  if (!dir.exists()) {
    assetsDir = QCoreApplication::applicationDirPath();
  }

  QString path = QFileDialog::getSaveFileName(
      this, tr("Save Scene"), assetsDir,
      SceneSerializerFactory::instance().getAllFilters());

  if (path.isEmpty())
    return false;

  // Add extension if not present
  if (!path.contains('.')) {
    path += ".xml";
  }

  ISceneSerializer *serializer =
      SceneSerializerFactory::instance().getSerializer(path);
  if (!serializer) {
    serializer =
        SceneSerializerFactory::instance().getSerializerByExtension("xml");
  }

  if (!serializer) {
    QMessageBox::warning(this, tr("Error"), tr("No serializer available"));
    return false;
  }

  SerializationResult result = serializer->save(path, m_document.get());
  if (!result.success) {
    QMessageBox::warning(
        this, tr("Error"),
        tr("Failed to save scene: %1").arg(result.errorMessage));
    return false;
  }

  setCurrentFile(path);
  m_commandHistory->setClean();
  m_modified = false;
  emit documentSaved();
  return true;
}

bool SceneEditorWidget::closeScene() {
  if (!maybeSave())
    return false;

  m_document->clear();
  m_commandHistory->clear();
  m_selectionManager->clearSelection();
  setCurrentFile(QString());

  emit documentChanged();
  return true;
}

// ===== Edit Operations =====

void SceneEditorWidget::undo() { m_commandHistory->undo(); }

void SceneEditorWidget::redo() { m_commandHistory->redo(); }

void SceneEditorWidget::cut() {
  // TODO: Implement clipboard operations
  copy();
  deleteSelected();
}

void SceneEditorWidget::copy() {
  // TODO: Implement clipboard operations
}

void SceneEditorWidget::paste() {
  // TODO: Implement clipboard operations
}

void SceneEditorWidget::deleteSelected() {
  auto selected = m_selectionManager->selectedNodes();
  if (selected.empty())
    return;

  if (selected.size() > 1) {
    m_commandHistory->beginMacro(tr("Delete %1 objects").arg(selected.size()));
  }

  for (SceneNode *node : selected) {
    auto cmd = std::make_unique<DeleteNodeCommand>(m_document.get(), node);
    m_commandHistory->execute(std::move(cmd));
  }

  if (selected.size() > 1) {
    m_commandHistory->endMacro();
  }

  m_selectionManager->clearSelection();
}

void SceneEditorWidget::duplicateSelected() {
  // TODO: Implement node duplication with clone method
}

void SceneEditorWidget::selectAll() { m_selectionManager->selectAll(); }

void SceneEditorWidget::deselectAll() { m_selectionManager->clearSelection(); }

// ===== Add Objects =====

void SceneEditorWidget::addSphere() {
  auto node = SceneNode::createSphere(QStringLiteral("Sphere"));
  auto cmd = std::make_unique<AddNodeCommand>(m_document.get(), std::move(node),
                                              nullptr);
  m_commandHistory->execute(std::move(cmd));
}

void SceneEditorWidget::addTriangle() {
  auto node = SceneNode::createTriangle(QStringLiteral("Triangle"));
  auto cmd = std::make_unique<AddNodeCommand>(m_document.get(), std::move(node),
                                              nullptr);
  m_commandHistory->execute(std::move(cmd));
}

void SceneEditorWidget::addMesh() {
  // Default to assets directory
  QString assetsDir =
      QCoreApplication::applicationDirPath() + QStringLiteral("/../assets");
  QDir dir(assetsDir);
  if (!dir.exists()) {
    assetsDir = QCoreApplication::applicationDirPath();
  }

  QString file = QFileDialog::getOpenFileName(
      this, tr("Select Mesh File"), assetsDir, tr("OBJ Files (*.obj)"));
  if (file.isEmpty())
    return;

  auto node = SceneNode::createMesh(QFileInfo(file).baseName(), file);

  auto cmd = std::make_unique<AddNodeCommand>(m_document.get(), std::move(node),
                                              nullptr);
  m_commandHistory->execute(std::move(cmd));
}

void SceneEditorWidget::addGroup() {
  auto node = SceneNode::createGroup(QStringLiteral("Group"));
  auto cmd = std::make_unique<AddNodeCommand>(m_document.get(), std::move(node),
                                              nullptr);
  m_commandHistory->execute(std::move(cmd));
}

// ===== View Operations =====

void SceneEditorWidget::resetView() { m_viewport->resetCamera(); }

void SceneEditorWidget::focusOnSelection() { m_viewport->focusOnSelection(); }

void SceneEditorWidget::toggleGrid() {
  m_viewport->setShowGrid(m_toggleGridAction->isChecked());
}

void SceneEditorWidget::toggleAxes() {
  m_viewport->setShowAxes(m_toggleAxesAction->isChecked());
}

// ===== Rendering =====

void SceneEditorWidget::renderScene() { emit renderRequested(); }

// ===== Private Slots =====

void SceneEditorWidget::onDocumentModified() {
  m_modified = true;
  emit documentModified();
  updateWindowTitle();
}

void SceneEditorWidget::onCommandExecuted() {
  m_modified = true;
  emit documentModified();
  updateWindowTitle();
}

void SceneEditorWidget::onUndoRedoChanged() {
  m_undoAction->setEnabled(m_commandHistory->canUndo());
  m_redoAction->setEnabled(m_commandHistory->canRedo());

  if (m_commandHistory->canUndo()) {
    m_undoAction->setText(
        tr("&Undo %1").arg(m_commandHistory->undoDescription()));
  } else {
    m_undoAction->setText(tr("&Undo"));
  }

  if (m_commandHistory->canRedo()) {
    m_redoAction->setText(
        tr("&Redo %1").arg(m_commandHistory->redoDescription()));
  } else {
    m_redoAction->setText(tr("&Redo"));
  }
}

// ===== Private Methods =====

void SceneEditorWidget::updateWindowTitle() {
  QString title = m_currentFilePath.isEmpty()
                      ? tr("Untitled")
                      : QFileInfo(m_currentFilePath).fileName();
  if (hasUnsavedChanges()) {
    title += " *";
  }
  // This would typically update the parent window's title
}

void SceneEditorWidget::updateActionStates() {
  bool hasSelection = !m_selectionManager->isEmpty();
  m_deleteAction->setEnabled(hasSelection);
  m_duplicateAction->setEnabled(hasSelection);
}

bool SceneEditorWidget::maybeSave() {
  if (!hasUnsavedChanges())
    return true;

  QMessageBox::StandardButton ret = QMessageBox::warning(
      this, tr("Scene Editor"),
      tr("The scene has been modified.\nDo you want to save your changes?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);

  switch (ret) {
  case QMessageBox::Save:
    return saveScene();
  case QMessageBox::Discard:
    return true;
  case QMessageBox::Cancel:
  default:
    return false;
  }
}

void SceneEditorWidget::setCurrentFile(const QString &filePath) {
  m_currentFilePath = filePath;
  updateWindowTitle();
}

} // namespace scene
} // namespace Raytracer
