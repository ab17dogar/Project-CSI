#include "ui/views/MainWindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>
#include <cmath>
#include <limits>

#include "ui/commands/CommandHistory.h"
#include "ui/controllers/RenderController.h"
#include "ui/controllers/ViewportCameraController.h"
#include "ui/models/PresetRepository.h"
#include "ui/models/RenderSettingsModel.h"
#include "ui/models/SceneDocument.h"
#include "ui/serialization/SceneSerializer.h"
#include "ui/services/SceneConverter.h"
#include "ui/views/RenderSettingsPanel.h"
#include "ui/views/RenderViewportWidget.h"
#include "ui/views/SceneEditorWidget.h"
#include "ui/views/SceneViewport.h"

namespace Raytracer {
namespace ui {

namespace {
constexpr int kMaxRecentScenes = 8;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_presetRepository(std::make_unique<PresetRepository>()),
      m_renderSettings(
          std::make_unique<RenderSettingsModel>(*m_presetRepository)),
      m_renderController(std::make_unique<RenderController>(*m_renderSettings)),
      m_sceneImporter(std::make_unique<SceneImportService>()) {
  loadScenePreferences();
  if (!m_lastPreset.isEmpty()) {
    m_renderSettings->setPreset(m_lastPreset);
  }

  setWindowTitle(tr("Project CSI Raytracer"));
  resize(1280, 720);
  setAcceptDrops(true);

  buildUi();
  wireSignals();

  if (!m_lastScenePath.isEmpty()) {
    applyScenePath(m_lastScenePath, false);
  }
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event) {
  if (maybeSave()) {
    // Stop any ongoing render before closing
    if (m_renderController) {
      m_renderController->stopRender();
    }
    event->accept();
  } else {
    event->ignore();
  }
}

bool MainWindow::maybeSave() {
  // Check if Scene Editor document has unsaved changes
  if (!m_sceneEditor || !m_sceneEditor->document()) {
    return true; // No document, safe to close
  }

  if (!m_sceneEditor->document()->isDirty()) {
    return true; // No unsaved changes
  }

  // Show confirmation dialog
  QMessageBox msgBox(this);
  msgBox.setIcon(QMessageBox::Warning);
  msgBox.setWindowTitle(tr("Unsaved Changes"));
  msgBox.setText(tr("The scene has been modified."));
  msgBox.setInformativeText(tr("Do you want to save your changes?"));
  msgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard |
                            QMessageBox::Cancel);
  msgBox.setDefaultButton(QMessageBox::Save);

  const int ret = msgBox.exec();
  switch (ret) {
  case QMessageBox::Save:
    // Try to save
    if (m_sceneEditor->saveScene()) {
      return true; // Saved successfully
    }
    return false; // Save failed or cancelled
  case QMessageBox::Discard:
    return true; // User chose not to save
  case QMessageBox::Cancel:
  default:
    return false; // User cancelled
  }
}

void MainWindow::buildUi() {
  auto *fileMenu = menuBar()->addMenu(tr("&File"));
  fileMenu->addAction(tr("Open Scene"), this, [this]() {
    const QString current = m_renderSettings->scenePath();
    const QString startDir = current.isEmpty()
                                 ? QDir::currentPath()
                                 : QFileInfo(current).absolutePath();
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Select Scene"), startDir,
                                     tr("Scene Files (*.xml);;All Files (*)"));
    if (path.isEmpty()) {
      return;
    }
    applyScenePath(path);
  });

  fileMenu->addAction(tr("Import Scene Bundle…"), this,
                      [this]() { handleImportSceneBundle(); });
  fileMenu->addAction(tr("Import Mesh (OBJ)…"), this,
                      [this]() { handleImportMeshScene(); });

  m_recentScenesMenu = fileMenu->addMenu(tr("Recent Scenes"));
  refreshRecentScenesMenu();

  fileMenu->addSeparator();
  fileMenu->addAction(tr("Quit"), this, &QWidget::close, QKeySequence::Quit);

  // ===== Edit Menu =====
  auto *editMenu = menuBar()->addMenu(tr("&Edit"));

  QAction *undoAction = editMenu->addAction(
      tr("Undo"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->undo();
      },
      QKeySequence::Undo);
  undoAction->setEnabled(false);

  QAction *redoAction = editMenu->addAction(
      tr("Redo"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->redo();
      },
      QKeySequence::Redo);
  redoAction->setEnabled(false);

  editMenu->addSeparator();

  editMenu->addAction(
      tr("Delete"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->deleteSelected();
      },
      QKeySequence::Delete);

  editMenu->addAction(
      tr("Duplicate"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->duplicateSelected();
      },
      QKeySequence(Qt::CTRL | Qt::Key_D));

  editMenu->addSeparator();

  editMenu->addAction(
      tr("Select All"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->selectAll();
      },
      QKeySequence::SelectAll);

  // Connect undo/redo state updates
  if (m_sceneEditor && m_sceneEditor->commandHistory()) {
    connect(m_sceneEditor->commandHistory(),
            &scene::CommandHistory::canUndoChanged, undoAction,
            &QAction::setEnabled);
    connect(m_sceneEditor->commandHistory(),
            &scene::CommandHistory::canRedoChanged, redoAction,
            &QAction::setEnabled);
  }

  // ===== View Menu =====
  auto *viewMenu = menuBar()->addMenu(tr("&View"));

  viewMenu->addAction(
      tr("Focus on Selection"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->focusOnSelection();
      },
      Qt::Key_F);

  viewMenu->addAction(
      tr("Reset View"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->resetView();
      },
      Qt::Key_Home);

  viewMenu->addSeparator();

  QAction *gridAction = viewMenu->addAction(
      tr("Toggle Grid"), this,
      [this]() {
        if (m_sceneEditor)
          m_sceneEditor->toggleGrid();
      },
      Qt::Key_G);
  gridAction->setCheckable(true);
  gridAction->setChecked(true);

  QAction *axesAction = viewMenu->addAction(tr("Toggle Axes"), this, [this]() {
    if (m_sceneEditor)
      m_sceneEditor->toggleAxes();
  });
  axesAction->setCheckable(true);
  axesAction->setChecked(true);

  auto *toolbar = addToolBar(tr("Render"));
  m_startAction = toolbar->addAction(tr("Start"));
  connect(m_startAction, &QAction::triggered, this,
          [this]() { m_renderController->startRender(); });

  m_stopAction = toolbar->addAction(tr("Stop"));
  connect(m_stopAction, &QAction::triggered, this,
          [this]() { m_renderController->stopRender(); });

  // Create main central widget with tabs
  m_centralWidget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(m_centralWidget);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  m_tabWidget = new QTabWidget(m_centralWidget);
  m_tabWidget->setDocumentMode(true); // Cleaner tab appearance

  // ===== Tab 1: Render View =====
  m_renderViewTab = new QWidget(m_tabWidget);
  auto *renderLayout = new QHBoxLayout(m_renderViewTab);
  renderLayout->setContentsMargins(0, 0, 0, 0);
  renderLayout->setSpacing(0);

  m_splitter = new QSplitter(Qt::Horizontal, m_renderViewTab);
  m_splitter->setChildrenCollapsible(true); // Allow collapsing for animation
  m_splitter->setHandleWidth(1);

  // Create a container for the viewport and toggle button
  auto *viewportContainer = new QWidget(m_splitter);
  auto *viewportLayout = new QHBoxLayout(viewportContainer);
  viewportLayout->setContentsMargins(0, 0, 0, 0);
  viewportLayout->setSpacing(0);

  m_viewport = new RenderViewportWidget(viewportContainer);

  // Create toggle button (arrow) on the right edge of viewport
  m_toggleButton = new QPushButton(viewportContainer);
  m_toggleButton->setFixedWidth(20);
  m_toggleButton->setText(
      QStringLiteral("◀")); // Panel visible initially, arrow points to hide
  m_toggleButton->setToolTip(tr("Hide Settings Panel"));
  m_toggleButton->setCursor(Qt::PointingHandCursor);
  m_toggleButton->setStyleSheet(QStringLiteral("QPushButton {"
                                               "  background-color: #3a3a3a;"
                                               "  color: #aaaaaa;"
                                               "  border: none;"
                                               "  border-radius: 0px;"
                                               "  font-size: 12px;"
                                               "  padding: 0px;"
                                               "}"
                                               "QPushButton:hover {"
                                               "  background-color: #4a4a4a;"
                                               "  color: #ffffff;"
                                               "}"
                                               "QPushButton:pressed {"
                                               "  background-color: #2a2a2a;"
                                               "}"));

  viewportLayout->addWidget(m_viewport, 1);
  viewportLayout->addWidget(m_toggleButton, 0);
  viewportContainer->setLayout(viewportLayout);

  m_settingsPanel = new RenderSettingsPanel(
      *m_renderSettings, *m_renderController, *m_presetRepository, m_splitter);
  m_settingsPanel->setMinimumWidth(200);
  m_settingsPanel->setMaximumWidth(400);

  m_splitter->addWidget(viewportContainer);
  m_splitter->addWidget(m_settingsPanel);

  // Set 80/20 ratio
  m_splitter->setStretchFactor(0, 4);
  m_splitter->setStretchFactor(1, 1);

  // Set initial sizes: 80% viewport, 20% settings panel
  m_splitter->setSizes({1024, 256}); // 80/20 of 1280
  m_savedPanelWidth = 256;

  renderLayout->addWidget(m_splitter);
  m_renderViewTab->setLayout(renderLayout);

  m_tabWidget->addTab(m_renderViewTab, tr("🖼️ Render View"));

  // ===== Tab 2: Scene Editor =====
  m_sceneEditor = new scene::SceneEditorWidget(m_tabWidget);
  m_tabWidget->addTab(m_sceneEditor, tr("🎨 Scene Editor"));

  // Connect scene editor render request
  connect(m_sceneEditor, &scene::SceneEditorWidget::renderRequested, this,
          &MainWindow::handleSceneEditorRenderRequest);

  mainLayout->addWidget(m_tabWidget);
  m_centralWidget->setLayout(mainLayout);
  setCentralWidget(m_centralWidget);

  // Connect toggle button
  connect(m_toggleButton, &QPushButton::clicked, this,
          &MainWindow::toggleSettingsPanel);

  setRenderControlsActive(false);
  statusBar()->showMessage(tr("Ready"));
}

void MainWindow::wireSignals() {
  connect(m_renderSettings.get(), &RenderSettingsModel::presetChanged, this,
          [this](const QString &preset) {
            m_lastPreset = preset;
            saveScenePreferences();
          });

  connect(m_renderController.get(), &RenderController::renderStarted, this,
          [this]() {
            statusBar()->showMessage(tr("Rendering…"));
            setRenderControlsActive(true);
          });

  connect(
      m_renderController.get(), &RenderController::renderFinished, this,
      [this]() {
        // Show render completion with timing and acceleration method
        const QString accelMethod =
            m_lastTelemetry.useBVH ? tr("BVH") : tr("Linear");
        const double totalSeconds = m_lastTelemetry.elapsedSeconds;
        QString timeStr;
        if (totalSeconds < 60.0) {
          timeStr = tr("%1 seconds").arg(QString::number(totalSeconds, 'f', 2));
        } else if (totalSeconds < 3600.0) {
          int mins = static_cast<int>(totalSeconds) / 60;
          double secs = totalSeconds - (mins * 60);
          timeStr = tr("%1m %2s").arg(mins).arg(QString::number(secs, 'f', 1));
        } else {
          int hours = static_cast<int>(totalSeconds) / 3600;
          int mins = (static_cast<int>(totalSeconds) % 3600) / 60;
          timeStr = tr("%1h %2m").arg(hours).arg(mins);
        }
        const QString message =
            tr("✓ Render complete — %1 (%2) — %3×%4 @ %5 samples")
                .arg(timeStr)
                .arg(accelMethod)
                .arg(m_lastTelemetry.stageWidth)
                .arg(m_lastTelemetry.stageHeight)
                .arg(m_lastTelemetry.stageSamples);
        statusBar()->showMessage(message, 30000); // Show for 30 seconds

        // Also print to console for easy comparison
        qDebug() << "=== RENDER COMPLETE ==="
                 << "\n  Time:" << timeStr << "\n  Acceleration:" << accelMethod
                 << "\n  Resolution:" << m_lastTelemetry.stageWidth << "x"
                 << m_lastTelemetry.stageHeight
                 << "\n  Samples:" << m_lastTelemetry.stageSamples;

        setRenderControlsActive(false);
      });

  connect(m_renderController.get(), &RenderController::frameReady, m_viewport,
          &RenderViewportWidget::setFrame);

  connect(m_renderController.get(), &RenderController::progressUpdated, this,
          [this](double completion, double etaSeconds) {
            if (!std::isfinite(completion)) {
              completion = 0.0;
            }
            const int percent =
                static_cast<int>(std::round(completion * 100.0));
            statusBar()->showMessage(tr("Rendering… %1% (ETA %2)")
                                         .arg(percent)
                                         .arg(formatEta(etaSeconds)));
          });

  connect(m_renderController.get(), &RenderController::renderFailed, this,
          [this](const QString &message) {
            setRenderControlsActive(false);
            QMessageBox::critical(this, tr("Render Error"), message);
            statusBar()->showMessage(tr("Render failed"), 5000);
          });

  connect(m_renderController.get(), &RenderController::renderStageChanged, this,
          [this](const QString &label) {
            statusBar()->showMessage(tr("%1 pass running…").arg(label));
          });

  connect(m_renderController.get(), &RenderController::telemetryUpdated, this,
          [this](const RendererService::RenderTelemetry &telemetry) {
            m_lastTelemetry = telemetry;
            updateStatusBarTelemetry(telemetry);
          });

  // Connect import asset signals from settings panel
  connect(
      m_settingsPanel, &RenderSettingsPanel::importXmlRequested, this,
      [this](const QString &path) { handleImportSceneBundleFromPath(path); });
  connect(m_settingsPanel, &RenderSettingsPanel::importObjRequested, this,
          [this](const QString &path) { handleImportMeshSceneFromPath(path); });
  connect(m_settingsPanel, &RenderSettingsPanel::importMtlRequested, this,
          [this](const QString &path) { handleImportMtl(path); });
}

void MainWindow::updateStatusBarTelemetry(
    const RendererService::RenderTelemetry &telemetry) {
  const int percent =
      static_cast<int>(std::round(telemetry.overallProgress * 100.0));
  const QString stageText = telemetry.totalStages > 1
                                ? tr("%1 (%2/%3)")
                                      .arg(telemetry.stageLabel)
                                      .arg(telemetry.stageIndex + 1)
                                      .arg(telemetry.totalStages)
                                : telemetry.stageLabel;

  const QString eta = formatEta(telemetry.estimatedRemainingSeconds);
  double estimatedStageSeconds =
      telemetry.stageProgress > 0.0
          ? telemetry.stageElapsedSeconds /
                std::max(telemetry.stageProgress, 0.01)
          : std::numeric_limits<double>::quiet_NaN();
  const QString throughput = formatThroughput(
      telemetry.stageWidth, telemetry.stageHeight, estimatedStageSeconds);

  statusBar()->showMessage(tr("%1 — %2% (ETA %3, %4)")
                               .arg(stageText)
                               .arg(percent)
                               .arg(eta)
                               .arg(throughput));
}

QString MainWindow::formatEta(double seconds) const {
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return tr("estimating…");
  }
  if (seconds < 120.0) {
    return tr("%1 s").arg(QString::number(seconds, 'f', 1));
  }

  const int totalSeconds = static_cast<int>(std::round(seconds));
  const int minutes = totalSeconds / 60;
  const int secs = totalSeconds % 60;
  if (minutes < 60) {
    return tr("%1m %2s").arg(minutes).arg(secs, 2, 10, QLatin1Char('0'));
  }

  const int hours = minutes / 60;
  const int remMinutes = minutes % 60;
  return tr("%1h %2m").arg(hours).arg(remMinutes);
}

QString MainWindow::formatThroughput(int width, int height,
                                     double stageTotalSeconds) const {
  if (width <= 0 || height <= 0 || !std::isfinite(stageTotalSeconds) ||
      stageTotalSeconds <= 0.0) {
    return tr("throughput n/a");
  }

  const double pixels =
      static_cast<double>(width) * static_cast<double>(height);
  const double megapixelsPerSec = (pixels / 1'000'000.0) / stageTotalSeconds;
  if (megapixelsPerSec >= 1.0) {
    return tr("%1 MP/s").arg(QString::number(megapixelsPerSec, 'f', 2));
  }

  const double kilopixelsPerSec = megapixelsPerSec * 1000.0;
  return tr("%1 kP/s").arg(QString::number(kilopixelsPerSec, 'f', 1));
}

void MainWindow::setRenderControlsActive(bool rendering) {
  if (m_startAction) {
    m_startAction->setEnabled(!rendering);
  }
  if (m_stopAction) {
    m_stopAction->setEnabled(rendering);
  }
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
  if (!event->mimeData() || !event->mimeData()->hasUrls()) {
    event->ignore();
    return;
  }

  for (const QUrl &url : event->mimeData()->urls()) {
    if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".xml"),
                                                        Qt::CaseInsensitive)) {
      event->acceptProposedAction();
      return;
    }
  }

  event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event) {
  if (!event->mimeData() || !event->mimeData()->hasUrls()) {
    event->ignore();
    return;
  }

  for (const QUrl &url : event->mimeData()->urls()) {
    if (!url.isLocalFile()) {
      continue;
    }
    const QString path = url.toLocalFile();
    if (!path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) {
      continue;
    }
    applyScenePath(path);
    event->acceptProposedAction();
    return;
  }

  event->ignore();
}

void MainWindow::applyScenePath(const QString &path, bool userInitiated) {
  if (path.isEmpty()) {
    return;
  }

  QFileInfo info(path);
  if (!info.isAbsolute()) {
    info = QFileInfo(info.absoluteFilePath());
  }

  if (!info.exists()) {
    if (userInitiated) {
      QMessageBox::warning(
          this, tr("Scene Not Found"),
          tr("Could not find scene file:\n%1").arg(info.absoluteFilePath()));
    }
    return;
  }

  m_renderSettings->setScenePath(info.absoluteFilePath());
  addRecentScene(info.absoluteFilePath());

  const QString label = info.fileName();
  if (userInitiated) {
    statusBar()->showMessage(tr("Scene loaded: %1").arg(label), 3000);
  } else {
    statusBar()->showMessage(tr("Scene ready: %1").arg(label), 2000);
  }
}

void MainWindow::addRecentScene(const QString &path) {
  QFileInfo info(path);
  if (!info.exists()) {
    return;
  }

  const QString normalized = info.absoluteFilePath();
  m_recentScenes.removeAll(normalized);
  m_recentScenes.prepend(normalized);
  while (m_recentScenes.size() > kMaxRecentScenes) {
    m_recentScenes.removeLast();
  }
  m_lastScenePath = normalized;

  refreshRecentScenesMenu();
  saveScenePreferences();
}

void MainWindow::refreshRecentScenesMenu() {
  if (!m_recentScenesMenu) {
    return;
  }

  QStringList cleaned;
  cleaned.reserve(m_recentScenes.size());
  for (const QString &path : m_recentScenes) {
    QFileInfo info(path);
    if (info.exists()) {
      cleaned << info.absoluteFilePath();
    }
  }
  if (cleaned != m_recentScenes) {
    m_recentScenes = cleaned;
    saveScenePreferences();
  }

  m_recentScenesMenu->clear();

  if (m_recentScenes.isEmpty()) {
    QAction *placeholder =
        m_recentScenesMenu->addAction(tr("No recent scenes"));
    placeholder->setEnabled(false);
    return;
  }

  for (const QString &path : m_recentScenes) {
    QFileInfo info(path);
    QAction *action = m_recentScenesMenu->addAction(info.fileName());
    action->setToolTip(info.absoluteFilePath());
    connect(action, &QAction::triggered, this,
            [this, path]() { applyScenePath(path); });
  }

  m_recentScenesMenu->addSeparator();
  m_recentScenesMenu->addAction(tr("Clear Recent Scenes"), this, [this]() {
    m_recentScenes.clear();
    m_lastScenePath.clear();
    refreshRecentScenesMenu();
    saveScenePreferences();
  });
}

void MainWindow::handleImportSceneBundle() {
  const QString startDir =
      m_renderSettings->scenePath().isEmpty()
          ? QDir::currentPath()
          : QFileInfo(m_renderSettings->scenePath()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Import Scene Bundle"), startDir,
                                   tr("Scene Files (*.xml);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  handleImportSceneBundleFromPath(path);
}

void MainWindow::handleImportSceneBundleFromPath(const QString &path) {
  if (path.isEmpty()) {
    return;
  }

  if (!m_sceneImporter) {
    QMessageBox::warning(this, tr("Import Error"),
                         tr("Importer not available."));
    return;
  }

  const auto result = m_sceneImporter->importScene(path);
  handleImportResult(result, tr("Import Scene Bundle"));
}

void MainWindow::handleImportMeshScene() {
  const QString startDir =
      m_renderSettings->scenePath().isEmpty()
          ? QDir::currentPath()
          : QFileInfo(m_renderSettings->scenePath()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Import Mesh (OBJ)"), startDir,
                                   tr("Mesh Files (*.obj);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  handleImportMeshSceneFromPath(path);
}

void MainWindow::handleImportMeshSceneFromPath(const QString &path) {
  if (path.isEmpty()) {
    return;
  }

  if (!m_sceneImporter) {
    QMessageBox::warning(this, tr("Import Error"),
                         tr("Importer not available."));
    return;
  }

  const auto result = m_sceneImporter->importMeshScene(path);
  handleImportResult(result, tr("Import Mesh"));
}

void MainWindow::handleImportMtl(const QString &path) {
  if (path.isEmpty()) {
    return;
  }

  // For MTL files, copy directly to assets/ directory (flat structure)
  QFileInfo info(path);
  if (!info.exists()) {
    QMessageBox::warning(this, tr("Import Error"),
                         tr("Material file not found: %1").arg(path));
    return;
  }

  QString assetsRoot;
  QStringList probeDirs;
  probeDirs << QDir::currentPath();
  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    probeDirs << appDir;
    QDir temp(appDir);
    for (int i = 0; i < 4; ++i) {
      temp.cdUp();
      probeDirs << temp.absolutePath();
    }
  }

  for (const QString &basePath : std::as_const(probeDirs)) {
    QDir dir(basePath);
    if (dir.exists(QStringLiteral("assets"))) {
      assetsRoot = dir.filePath(QStringLiteral("assets"));
      break;
    }
  }

  if (assetsRoot.isEmpty()) {
    QDir cwd(QDir::currentPath());
    cwd.mkpath(QStringLiteral("assets"));
    assetsRoot = cwd.filePath(QStringLiteral("assets"));
  }

  // Copy directly to assets/ (no subdirectory)
  const QString destPath = QDir(assetsRoot).filePath(info.fileName());
  if (QFile::exists(destPath)) {
    QFile::remove(destPath);
  }

  if (!QFile::copy(path, destPath)) {
    QMessageBox::warning(
        this, tr("Import Error"),
        tr("Failed to copy material file to %1").arg(destPath));
    return;
  }

  QMessageBox::information(this, tr("Import Material"),
                           tr("Material file imported to:\n%1")
                               .arg(QDir::toNativeSeparators(destPath)));
  statusBar()->showMessage(tr("Material imported: %1").arg(info.fileName()),
                           3000);
}

void MainWindow::handleImportResult(
    const SceneImportService::ImportResult &result,
    const QString &contextLabel) {
  if (result.importedScenePath.isEmpty()) {
    const QString detail = result.error.isEmpty()
                               ? tr("Unknown error during import.")
                               : result.error;
    QMessageBox::warning(this, contextLabel, detail);
    return;
  }

  if (!result.missingFiles.isEmpty()) {
    QMessageBox::information(
        this, contextLabel,
        tr("Scene imported to %1 but the following files were missing:\n%2")
            .arg(QDir::toNativeSeparators(result.importedScenePath))
            .arg(result.missingFiles.join(QLatin1String("\n"))));
  } else {
    QMessageBox::information(
        this, contextLabel,
        tr("Scene copied to %1")
            .arg(QDir::toNativeSeparators(result.importedScenePath)));
  }

  applyScenePath(result.importedScenePath);
}

void MainWindow::loadScenePreferences() {
  QSettings settings(QStringLiteral("Project-CSI"),
                     QStringLiteral("Raytracer"));
  m_recentScenes =
      settings.value(QStringLiteral("ui/recentScenes")).toStringList();
  m_lastScenePath = settings.value(QStringLiteral("ui/lastScene")).toString();
  m_lastPreset =
      settings.value(QStringLiteral("ui/lastPreset"), QStringLiteral("Custom"))
          .toString();
}

void MainWindow::saveScenePreferences() const {
  QSettings settings(QStringLiteral("Project-CSI"),
                     QStringLiteral("Raytracer"));
  settings.setValue(QStringLiteral("ui/recentScenes"), m_recentScenes);
  settings.setValue(QStringLiteral("ui/lastScene"), m_lastScenePath);
  settings.setValue(QStringLiteral("ui/lastPreset"), m_lastPreset);
}

void MainWindow::toggleSettingsPanel() {
  animateSettingsPanel(!m_panelVisible);
}

void MainWindow::animateSettingsPanel(bool show) {
  if (m_panelAnimation &&
      m_panelAnimation->state() == QVariantAnimation::Running) {
    m_panelAnimation->stop();
    delete m_panelAnimation;
    m_panelAnimation = nullptr;
  }

  const QList<int> currentSizes = m_splitter->sizes();
  const int totalWidth = currentSizes[0] + currentSizes[1];

  int startPanelWidth = currentSizes[1];
  int endPanelWidth = 0;

  if (show) {
    // Restore panel - use saved width or 20% of total
    endPanelWidth = m_savedPanelWidth > 0 ? m_savedPanelWidth : totalWidth / 5;
    m_toggleButton->setText(QStringLiteral("◀"));
    m_toggleButton->setToolTip(tr("Hide Settings Panel"));
    m_panelVisible = true;
    m_settingsPanel->show();
  } else {
    // Hide panel - save current width first
    m_savedPanelWidth = currentSizes[1];
    endPanelWidth = 0;
    m_toggleButton->setText(QStringLiteral("▶"));
    m_toggleButton->setToolTip(tr("Show Settings Panel"));
    m_panelVisible = false;
  }

  // Create animation
  m_panelAnimation = new QVariantAnimation(this);
  m_panelAnimation->setDuration(250);
  m_panelAnimation->setEasingCurve(QEasingCurve::OutCubic);
  m_panelAnimation->setStartValue(startPanelWidth);
  m_panelAnimation->setEndValue(endPanelWidth);

  QSplitter *splitter = m_splitter;
  connect(m_panelAnimation, &QVariantAnimation::valueChanged, this,
          [splitter, totalWidth](const QVariant &value) {
            const int panelWidth = value.toInt();
            splitter->setSizes({totalWidth - panelWidth, panelWidth});
          });

  RenderSettingsPanel *panel = m_settingsPanel;
  connect(m_panelAnimation, &QVariantAnimation::finished, this,
          [this, show, panel]() {
            if (!show) {
              panel->hide();
            }
            m_panelAnimation->deleteLater();
            m_panelAnimation = nullptr;
          });

  m_panelAnimation->start();
}

void MainWindow::handleSceneEditorRenderRequest() {
  qDebug() << "=== handleSceneEditorRenderRequest called ===";

  if (!m_sceneEditor || !m_sceneEditor->document()) {
    qDebug() << "No scene editor or document!";
    return;
  }

  // ===== SYNC VIEWPORT CAMERA TO DOCUMENT =====
  // This ensures render output matches Scene Editor viewpoint
  if (auto *viewport = m_sceneEditor->viewport()) {
    if (auto *camCtrl = viewport->cameraController()) {
      QVector3D pos = camCtrl->position();
      QVector3D target = camCtrl->target();

      scene::CameraSettings cam = m_sceneEditor->document()->camera();
      cam.lookFrom = glm::vec3(pos.x(), pos.y(), pos.z());
      cam.lookAt = glm::vec3(target.x(), target.y(), target.z());
      cam.fov = 45.0f; // Match viewport perspective FOV
      m_sceneEditor->document()->setCamera(cam);

      qDebug() << "Camera synced: lookFrom=" << pos << "lookAt=" << target;
    }
  }
  // ===============================================

  QString scenePath = m_sceneEditor->currentFilePath();

  // If scene has a file path, sync any unsaved changes to it
  if (!scenePath.isEmpty() && m_sceneEditor->hasUnsavedChanges()) {
    if (!m_sceneEditor->saveScene()) {
      return; // User cancelled save
    }
  }

  // If no file path, auto-save to temp file in assets directory for preview
  // rendering
  if (scenePath.isEmpty()) {
    // Save to assets/temp.xml - gets overwritten on each render until user
    // saves explicitly
    QString assetsDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets");
    QDir dir(assetsDir);
    if (!dir.exists()) {
      dir.mkpath(assetsDir);
    }
    scenePath = dir.filePath(QStringLiteral("temp.xml"));
    qDebug() << "Creating temp preview file:" << scenePath;

    // Save to temp file using XML serializer
    scene::ISceneSerializer *serializer =
        scene::SceneSerializerFactory::instance().getSerializerByExtension(
            QStringLiteral("xml"));
    if (serializer) {
      scene::SerializationResult result =
          serializer->save(scenePath, m_sceneEditor->document());
      if (!result.success) {
        qDebug() << "Failed to save temp file:" << result.errorMessage;
        QMessageBox::warning(this, tr("Render Error"),
                             tr("Failed to save scene for rendering: %1")
                                 .arg(result.errorMessage));
        return;
      }
      qDebug() << "Temp file saved successfully!";
    } else {
      QMessageBox::warning(this, tr("Render Error"),
                           tr("No XML serializer available"));
      return;
    }
  }

  if (!scenePath.isEmpty()) {
    applyScenePath(scenePath);

    // Switch to render tab and start rendering
    m_tabWidget->setCurrentWidget(m_renderViewTab);
    m_renderController->startRender();
  }
}

} // namespace ui
} // namespace Raytracer
