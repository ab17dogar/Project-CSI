#pragma once

#include <QObject>
#include <memory>

#include "ui/models/RenderQueueModel.h"
#include "ui/services/remote/RemoteDispatchClient.h"
#include "ui/services/remote/RemoteCredentialsStore.h"

namespace Raytracer {
namespace ui {

class RemoteDispatchManager : public QObject
{
    Q_OBJECT

public:
    explicit RemoteDispatchManager(RemoteCredentialsStore &credentials, QObject *parent = nullptr);

    bool startJob(const RenderQueueModel::QueuedRender &job,
                  const RemoteWorkerDefinition &worker,
                  QString *error = nullptr);
    void cancel();
    bool isRunning() const noexcept { return m_running; }
    QString activeJobId() const { return m_payload.jobId; }

signals:
    void jobStarted(const QString &jobId);
    void jobProgress(const QString &jobId, double value, const QString &label);
    void jobCompleted(const RemoteDispatchResult &result);
    void jobFailed(const QString &jobId, const QString &message);

private:
    bool preparePayload(const RenderQueueModel::QueuedRender &job, RemoteDispatchPayload &payload, QString *error) const;
    RemoteDispatchClientPtr createClient(RemoteTransportMethod method);
    QString detectOutputFormat(const QString &path) const;

    RemoteCredentialsStore &m_credentials;
    RemoteDispatchClientPtr m_client;
    RemoteDispatchPayload m_payload;
    RemoteWorkerDefinition m_worker;
    bool m_running {false};
};

} // namespace ui
} // namespace Raytracer
