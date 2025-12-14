#ifndef RAYTRACER_SCENE_EDITOR_WIDGET_H
#define RAYTRACER_SCENE_EDITOR_WIDGET_H

#include <QAction>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QWidget>
#include <memory>

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneViewport;
class SceneHierarchyPanel;
class PropertiesPanel;
class LightingPanel;
class SelectionManager;
class CommandHistory;

/**
 * @brief Main scene editor widget that integrates all editor components
 *
 * This widget combines:
 * - 3D viewport for scene visualization
 * - Hierarchy panel for scene graph navigation
 * - Properties panel for object editing
 * - Toolbar for common operations
 * - Scene document management
 */
class SceneEditorWidget : public QWidget {
  Q_OBJECT

public:
  explicit SceneEditorWidget(QWidget *parent = nullptr);
  ~SceneEditorWidget() override;

  // Document management
  SceneDocument *document() const { return m_document.get(); }
  bool hasUnsavedChanges() const;
  QString currentFilePath() const { return m_currentFilePath; }

  // Component access
  SceneViewport *viewport() const { return m_viewport; }
  SceneHierarchyPanel *hierarchyPanel() const { return m_hierarchyPanel; }
  PropertiesPanel *propertiesPanel() const { return m_propertiesPanel; }
  LightingPanel *lightingPanel() const { return m_lightingPanel; }
  SelectionManager *selectionManager() const {
    return m_selectionManager.get();
  }
  CommandHistory *commandHistory() const { return m_commandHistory.get(); }

signals:
  void documentChanged();
  void documentModified();
  void documentSaved();
  void renderRequested();

public slots:
  // File operations
  void newScene();
  bool openScene(const QString &filePath = QString());
  bool saveScene();
  bool saveSceneAs();
  bool closeScene();

  // Edit operations
  void undo();
  void redo();
  void cut();
  void copy();
  void paste();
  void deleteSelected();
  void duplicateSelected();
  void selectAll();
  void deselectAll();

  // Add objects
  void addSphere();
  void addTriangle();
  void addMesh();
  void addGroup();

  // View operations
  void resetView();
  void focusOnSelection();
  void toggleGrid();
  void toggleAxes();

  // Rendering
  void renderScene();

private slots:
  void onDocumentModified();
  void onCommandExecuted();
  void onUndoRedoChanged();

private:
  void setupUI();
  void setupToolbar();
  void setupMenus();
  void setupConnections();
  void createActions();
  void updateWindowTitle();
  void updateActionStates();

  bool maybeSave();
  void setCurrentFile(const QString &filePath);

private:
  // Scene data
  std::unique_ptr<SceneDocument> m_document;
  std::unique_ptr<SelectionManager> m_selectionManager;
  std::unique_ptr<CommandHistory> m_commandHistory;

  // Widgets
  QToolBar *m_toolbar = nullptr;
  QSplitter *m_mainSplitter = nullptr;
  QSplitter *m_leftSplitter = nullptr;
  SceneViewport *m_viewport = nullptr;
  SceneHierarchyPanel *m_hierarchyPanel = nullptr;
  PropertiesPanel *m_propertiesPanel = nullptr;
  LightingPanel *m_lightingPanel = nullptr;

  // Actions
  QAction *m_newAction = nullptr;
  QAction *m_openAction = nullptr;
  QAction *m_saveAction = nullptr;
  QAction *m_saveAsAction = nullptr;
  QAction *m_undoAction = nullptr;
  QAction *m_redoAction = nullptr;
  QAction *m_deleteAction = nullptr;
  QAction *m_duplicateAction = nullptr;
  QAction *m_addSphereAction = nullptr;
  QAction *m_addTriangleAction = nullptr;
  QAction *m_addMeshAction = nullptr;
  QAction *m_addGroupAction = nullptr;
  QAction *m_renderAction = nullptr;
  QAction *m_toggleGridAction = nullptr;
  QAction *m_toggleAxesAction = nullptr;
  QAction *m_resetViewAction = nullptr;
  QAction *m_focusAction = nullptr;

  // State
  QString m_currentFilePath;
  bool m_modified = false;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SCENE_EDITOR_WIDGET_H
