#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>
#include <QtCore/qstringliteral.h>
#include <atomic>

namespace Raytracer {
namespace ui {

class RendererService : public QObject
{
    Q_OBJECT

public:
    struct RenderResult {
        QImage frame;
        QString error;
        bool cancelled {false};
    };

    struct RenderTelemetry {
        QString scenePath;
        QString stageLabel;
        int stageIndex {0};
        int totalStages {0};
        double stageProgress {0.0};
        double overallProgress {0.0};
        double stageElapsedSeconds {0.0};
        double elapsedSeconds {0.0};
        double estimatedRemainingSeconds {0.0};
        double avgTileMs {0.0};
        double tilesPerSecond {0.0};
        int stageWidth {0};
        int stageHeight {0};
        int stageSamples {0};
        bool finalStage {false};
        bool useBVH {false};  // Acceleration method used
    };

    struct RenderRequest {
        int width {0};
        int height {0};
        int samplesPerPixel {0};
        QString scenePath;
        bool enableDenoiser {false};
        QString toneMappingId {QStringLiteral("neutral")};
        bool useBVH {false};  // Use BVH acceleration instead of linear intersection
    };

public:
    explicit RendererService(QObject *parent = nullptr);

    void startRender(const RenderRequest &request);
    void stopRender();

    bool isRendering() const noexcept { return m_rendering.load(); }

signals:
    void renderStarted();
    void frameReady(const QImage &frame);
    void progressUpdated(double completionRatio, double etaSeconds);
    void renderFailed(const QString &message);
    void renderStageChanged(const QString &label);
    void renderFinished();
    void telemetryUpdated(const RenderTelemetry &telemetry);

private:
#if 0
    QImage applyPostProcessing(const QImage &frame, const RenderRequest &request) const;
    QImage applyAdaptiveDenoise(const QImage &image) const;
    QImage applyToneMapping(const QImage &image, const QString &toneMappingId) const;
#endif

    std::atomic<bool> m_rendering {false};
    std::atomic<bool> m_cancelRequested {false};
    QFutureWatcher<RenderResult> m_watcher;
    RenderRequest m_activeRequest;
};

} // namespace ui
} // namespace Raytracer
