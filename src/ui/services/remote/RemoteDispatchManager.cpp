#include "ui/services/remote/RemoteDispatchManager.h"

#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include "ui/services/remote/SshDispatchClient.h"
#include "ui/services/remote/HttpDispatchClient.h"

namespace Raytracer {
namespace ui {

RemoteDispatchManager::RemoteDispatchManager(RemoteCredentialsStore &credentials, QObject *parent)
    : QObject(parent)
    , m_credentials(credentials)
{
}

bool RemoteDispatchManager::startJob(const RenderQueueModel::QueuedRender &job,
                                     const RemoteWorkerDefinition &worker,
                                     QString *error)
{
    if (m_running) {
        if (error) {
            *error = tr("A remote job is already running");
        }
        return false;
    }

    RemoteDispatchPayload payload;
    if (!preparePayload(job, payload, error)) {
        return false;
    }

    m_worker = worker;
    m_payload = payload;
    m_client = createClient(worker.transport);
    if (!m_client) {
        if (error) {
            *error = tr("Unsupported remote transport");
        }
        return false;
    }

    if (!worker.credentialKey.isEmpty()) {
        QString credentialError;
        const QString token = m_credentials.fetchToken(worker.credentialKey, &credentialError);
        if (!credentialError.isEmpty() && error) {
            *error = credentialError;
            return false;
        }
        m_payload.authToken = token;
    }

        connect(m_client.get(), &RemoteDispatchClient::started,
            this, &RemoteDispatchManager::jobStarted);
        connect(m_client.get(), &RemoteDispatchClient::progress,
            this, &RemoteDispatchManager::jobProgress);
        connect(m_client.get(), &RemoteDispatchClient::completed,
                this, [this](const RemoteDispatchResult &result) {
                    m_running = false;
                    emit jobCompleted(result);
                    m_client.reset();
                });
        connect(m_client.get(), &RemoteDispatchClient::failed,
                this, [this](const QString &jobId, const QString &message) {
                    m_running = false;
                    emit jobFailed(jobId, message);
                    m_client.reset();
                });

    m_client->start(worker, m_payload);
    m_running = true;
    return true;
}

void RemoteDispatchManager::cancel()
{
    if (m_client) {
        m_client->cancel();
        m_client.reset();
    }
    m_running = false;
}

bool RemoteDispatchManager::preparePayload(const RenderQueueModel::QueuedRender &job,
                                           RemoteDispatchPayload &payload,
                                           QString *error) const
{
    QFile sceneFile(job.scenePath);
    if (!sceneFile.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = tr("Unable to read scene file %1").arg(job.scenePath);
        }
        return false;
    }

    payload.jobId = job.id.isNull() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : job.id.toString(QUuid::WithoutBraces);
    payload.scenePath = job.scenePath;
    payload.sceneFileName = QFileInfo(job.scenePath).fileName();
    payload.sceneContents = sceneFile.readAll();
    payload.width = job.width;
    payload.height = job.height;
    payload.samplesPerPixel = job.samples;
    payload.enableDenoiser = job.denoiserEnabled;
    payload.toneMappingId = job.toneMapping;
    payload.outputPath = job.outputPath;
    payload.outputFormat = detectOutputFormat(job.outputPath);
    payload.metadata.insert(QStringLiteral("preset"), job.presetLabel);
    payload.metadata.insert(QStringLiteral("queueId"), job.id.toString());
    payload.metadata.insert(QStringLiteral("scene"), QFileInfo(job.scenePath).fileName());
    payload.metadata.insert(QStringLiteral("tag"), job.remoteWorkerLabel);
    sceneFile.close();
    return true;
}

RemoteDispatchClientPtr RemoteDispatchManager::createClient(RemoteTransportMethod method)
{
    switch (method) {
    case RemoteTransportMethod::SSH:
        return std::make_unique<SshDispatchClient>();
    case RemoteTransportMethod::HTTP:
        return std::make_unique<HttpDispatchClient>();
    }
    return nullptr;
}

QString RemoteDispatchManager::detectOutputFormat(const QString &path) const
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("ppm")) {
        return QStringLiteral("ppm");
    }
    if (suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg")) {
        return QStringLiteral("jpg");
    }
    return QStringLiteral("png");
}

} // namespace ui
} // namespace Raytracer
