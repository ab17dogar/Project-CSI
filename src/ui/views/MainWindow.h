#pragma once


#include <memory>
#include <QMainWindow>

#include "ui/services/RendererService.h"
#include "ui/services/SceneImportService.h"

class QWidget;
class QAction;
class QMenu;
class QDragEnterEvent;
class QDropEvent;
class QSplitter;
class QPushButton;
class QVariantAnimation;
class QTabWidget;

namespace Raytracer {

namespace scene {
class SceneEditorWidget;
}

namespace ui {

class RenderSettingsModel;
class RenderController;
class RenderViewportWidget;
class RenderSettingsPanel;
class PresetRepository;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void wireSignals();
    void setRenderControlsActive(bool rendering);
    void applyScenePath(const QString &path, bool userInitiated = true);
    void addRecentScene(const QString &path);
    void refreshRecentScenesMenu();
    void loadScenePreferences();
    void saveScenePreferences() const;
    void updateStatusBarTelemetry(const RendererService::RenderTelemetry &telemetry);
    QString formatEta(double seconds) const;
    QString formatThroughput(int width, int height, double stageTotalSeconds) const;
    void handleImportSceneBundle();
    void handleImportSceneBundleFromPath(const QString &path);
    void handleImportMeshScene();
    void handleImportMeshSceneFromPath(const QString &path);
    void handleImportMtl(const QString &path);
    void handleImportResult(const SceneImportService::ImportResult &result,
                            const QString &contextLabel);
    void toggleSettingsPanel();
    void animateSettingsPanel(bool show);
    void handleSceneEditorRenderRequest();

private:
    QWidget *m_centralWidget {nullptr};
    QTabWidget *m_tabWidget {nullptr};
    
    // Render View tab
    QWidget *m_renderViewTab {nullptr};
    RenderViewportWidget *m_viewport {nullptr};
    RenderSettingsPanel *m_settingsPanel {nullptr};
    QSplitter *m_splitter {nullptr};
    QPushButton *m_toggleButton {nullptr};
    QVariantAnimation *m_panelAnimation {nullptr};
    int m_savedPanelWidth {0};
    bool m_panelVisible {true};
    
    // Scene Editor tab
    scene::SceneEditorWidget *m_sceneEditor {nullptr};
    
    std::unique_ptr<PresetRepository> m_presetRepository;
    std::unique_ptr<RenderSettingsModel> m_renderSettings;
    std::unique_ptr<RenderController> m_renderController;
    std::unique_ptr<SceneImportService> m_sceneImporter;
    QAction *m_startAction {nullptr};
    QAction *m_stopAction {nullptr};
    QMenu *m_recentScenesMenu {nullptr};
    QStringList m_recentScenes;
    QString m_lastScenePath;
    QString m_lastPreset {QStringLiteral("Custom")};
    RendererService::RenderTelemetry m_lastTelemetry;
};

} // namespace ui
} // namespace Raytracer
