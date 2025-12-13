#pragma once

#include <QObject>
#include <QImage>
#include <QString>
#include <QUuid>
#include <memory>
#include <optional>
#include <QVector>

#include "ui/models/RenderQueueModel.h"
#include "ui/services/RendererService.h"
#include "ui/services/remote/RemoteWorkerConfig.h"
#include "ui/services/remote/RemoteCredentialsStore.h"
#include "ui/services/remote/RemoteDispatchManager.h"
#include "ui/services/remote/RemoteDispatchCommon.h"

namespace Raytracer {
namespace ui {

class RenderSettingsModel;
class RenderController : public QObject
{
    Q_OBJECT

public:
    struct RemoteWorkerInfo {
        RemoteWorkerDefinition definition;
        int activeJobs {0};
        int queuedJobs {0};
        QString statusLabel;
        bool available {true};
    };

    explicit RenderController(RenderSettingsModel &settingsModel, QObject *parent = nullptr);

    RenderQueueModel &queueModel() noexcept { return *m_queue; }
    const RenderQueueModel &queueModel() const noexcept { return *m_queue; }
    const QVector<RemoteWorkerInfo> &remoteWorkers() const noexcept { return m_remoteWorkers; }
    RemoteWorkerConfigRepository &remoteWorkerRepository() noexcept { return *m_remoteConfig; }
    RemoteCredentialsStore &credentialsStore() noexcept { return *m_credentials; }
    bool isRemoteJobRunning() const noexcept { return m_remoteManager && m_remoteManager->isRunning(); }
    QString remoteWorkerLabelForId(const QString &workerId) const;

public slots:
    void startRender();
    void stopRender();
    QUuid queueCurrentSettings();
    QUuid queueCurrentSettingsWithOutput(const QString &outputPath);
    bool startQueuedRenders();
    void clearQueuedRenders();
    bool updateQueuedJobDispatch(const QUuid &id,
                                 RenderQueueModel::DispatchTarget target,
                                 const QString &workerId = {});

signals:
    void renderStarted();
    void renderFinished();
    void frameReady(const QImage &frame);
    void progressUpdated(double completionRatio, double etaSeconds);
    void renderFailed(const QString &message);
    void renderStageChanged(const QString &label);
    void queueProcessingChanged(bool running);
    void activeQueueJobChanged(const RenderQueueModel::QueuedRender &job);
    void queueDrained();
    void telemetryUpdated(const RendererService::RenderTelemetry &telemetry);
    void remoteWorkersChanged();

private slots:
    void handleRemoteJobStarted(const QString &jobId);
    void handleRemoteJobProgress(const QString &jobId, double value, const QString &label);
    void handleRemoteJobCompleted(const RemoteDispatchResult &result);
    void handleRemoteJobFailed(const QString &jobId, const QString &message);

private:
    void startNextQueuedRender();
    void handleServiceRenderFinished();
    void startLocalRender(const RenderQueueModel::QueuedRender &job);
    void startRemoteRender(const RenderQueueModel::QueuedRender &job);
    QString defaultOutputPathForJob(const RenderQueueModel::QueuedRender &job) const;
    bool saveFrameForJob(const RenderQueueModel::QueuedRender &job, const QImage &frame) const;
    void refreshRemoteWorkers();
    QString normalizedRemoteWorkerId(const QString &workerId) const;
    const RemoteWorkerInfo *findRemoteWorkerById(const QString &workerId) const;
    void applyRemoteSettings(RenderQueueModel::QueuedRender &job) const;
    void updateWorkerActivity(const QString &workerId, int activeDelta);
    void configureRemoteFields(RenderQueueModel::QueuedRender &job,
                               RenderQueueModel::DispatchTarget target,
                               const QString &workerId) const;

    RenderSettingsModel &m_settings;
    std::unique_ptr<RendererService> m_renderer;
    std::unique_ptr<RenderQueueModel> m_queue;
    bool m_queueProcessing {false};
    std::optional<RenderQueueModel::QueuedRender> m_activeQueuedJob;
    QImage m_lastFrame;
    std::unique_ptr<RemoteWorkerConfigRepository> m_remoteConfig;
    std::unique_ptr<RemoteCredentialsStore> m_credentials;
    std::unique_ptr<RemoteDispatchManager> m_remoteManager;
    QVector<RemoteWorkerInfo> m_remoteWorkers;
};

} // namespace ui
} // namespace Raytracer
