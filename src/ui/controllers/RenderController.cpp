#include "ui/controllers/RenderController.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMetaType>
#include <QRegularExpression>

#include <algorithm>

#include "ui/models/RenderQueueModel.h"
#include "ui/models/RenderSettingsModel.h"
#include "ui/services/RendererService.h"

namespace Raytracer {
namespace ui {

RenderController::RenderController(RenderSettingsModel &settingsModel, QObject *parent)
    : QObject(parent)
    , m_settings(settingsModel)
    , m_renderer(std::make_unique<RendererService>())
    , m_queue(std::make_unique<RenderQueueModel>())
    , m_remoteConfig(std::make_unique<RemoteWorkerConfigRepository>())
    , m_credentials(std::make_unique<RemoteCredentialsStore>())
    , m_remoteManager(std::make_unique<RemoteDispatchManager>(*m_credentials))
{
    qRegisterMetaType<RenderQueueModel::QueuedRender>("Raytracer::ui::RenderQueueModel::QueuedRender");
    connect(m_renderer.get(), &RendererService::renderStarted, this, &RenderController::renderStarted);
    connect(m_renderer.get(), &RendererService::renderFinished, this, &RenderController::renderFinished);
    connect(m_renderer.get(), &RendererService::frameReady, this, [this](const QImage &frame) {
        m_lastFrame = frame;
        emit frameReady(frame);
    });
    connect(m_renderer.get(), &RendererService::progressUpdated, this, &RenderController::progressUpdated);
    connect(m_renderer.get(), &RendererService::renderFailed, this, &RenderController::renderFailed);
    connect(m_renderer.get(), &RendererService::renderStageChanged, this, &RenderController::renderStageChanged);
    connect(m_renderer.get(), &RendererService::renderFinished, this, &RenderController::handleServiceRenderFinished);
    connect(m_renderer.get(), &RendererService::telemetryUpdated, this, &RenderController::telemetryUpdated);
        connect(m_remoteConfig.get(), &RemoteWorkerConfigRepository::workersChanged,
            this, &RenderController::refreshRemoteWorkers);
        if (m_remoteManager) {
        connect(m_remoteManager.get(), &RemoteDispatchManager::jobStarted,
            this, &RenderController::handleRemoteJobStarted);
        connect(m_remoteManager.get(), &RemoteDispatchManager::jobProgress,
            this, &RenderController::handleRemoteJobProgress);
        connect(m_remoteManager.get(), &RemoteDispatchManager::jobCompleted,
            this, &RenderController::handleRemoteJobCompleted);
        connect(m_remoteManager.get(), &RemoteDispatchManager::jobFailed,
            this, &RenderController::handleRemoteJobFailed);
        }
    refreshRemoteWorkers();
}

void RenderController::startRender()
{
    if (m_renderer->isRendering()) {
        return;
    }

    RendererService::RenderRequest request;
    request.width = m_settings.width();
    request.height = m_settings.height();
    request.samplesPerPixel = m_settings.samples();
    request.scenePath = m_settings.scenePath();
    request.enableDenoiser = m_settings.denoiserEnabled();
    request.toneMappingId = m_settings.toneMapping();
    request.useBVH = m_settings.useBVH();
    m_renderer->startRender(request);
}

void RenderController::stopRender()
{
    const bool rendererActive = m_renderer->isRendering();
    const bool remoteActive = isRemoteJobRunning();
    if (!rendererActive && !remoteActive) {
        if (m_queueProcessing) {
            m_queueProcessing = false;
            emit queueProcessingChanged(false);
        }
        return;
    }

    const bool wasQueueProcessing = m_queueProcessing;
    std::optional<RenderQueueModel::QueuedRender> jobToRequeue;
    if (wasQueueProcessing && m_activeQueuedJob) {
        jobToRequeue = m_activeQueuedJob;
        m_activeQueuedJob.reset();
    }
    m_queueProcessing = false;

    if (wasQueueProcessing) {
        emit queueProcessingChanged(false);
    }

    if (remoteActive && m_remoteManager) {
        if (m_activeQueuedJob && !m_activeQueuedJob->remoteWorkerId.isEmpty()) {
            updateWorkerActivity(m_activeQueuedJob->remoteWorkerId, -1);
        }
        m_remoteManager->cancel();
    } else {
        m_renderer->stopRender();
    }

    if (jobToRequeue) {
        m_queue->prepend(*jobToRequeue);
    }
}

QUuid RenderController::queueCurrentSettings()
{
    RenderQueueModel::QueuedRender job;
    job.scenePath = m_settings.scenePath();
    job.width = m_settings.width();
    job.height = m_settings.height();
    job.samples = m_settings.samples();
    job.presetLabel = m_settings.preset();
    job.outputPath = defaultOutputPathForJob(job);
    job.denoiserEnabled = m_settings.denoiserEnabled();
    job.toneMapping = m_settings.toneMapping();
    applyRemoteSettings(job);
    return m_queue->enqueue(job);
}

QUuid RenderController::queueCurrentSettingsWithOutput(const QString &outputPath)
{
    RenderQueueModel::QueuedRender job;
    job.scenePath = m_settings.scenePath();
    job.width = m_settings.width();
    job.height = m_settings.height();
    job.samples = m_settings.samples();
    job.presetLabel = m_settings.preset();
    job.outputPath = outputPath.isEmpty() ? defaultOutputPathForJob(job) : outputPath;
    job.denoiserEnabled = m_settings.denoiserEnabled();
    job.toneMapping = m_settings.toneMapping();
    applyRemoteSettings(job);
    return m_queue->enqueue(job);
}

bool RenderController::startQueuedRenders()
{
    if (m_queueProcessing || m_renderer->isRendering() || isRemoteJobRunning()) {
        return false;
    }
    if (m_queue->isEmpty()) {
        return false;
    }

    m_queueProcessing = true;
    emit queueProcessingChanged(true);
    startNextQueuedRender();
    return true;
}

void RenderController::clearQueuedRenders()
{
    if (m_queueProcessing) {
        return;
    }
    m_queue->clear();
}

bool RenderController::updateQueuedJobDispatch(const QUuid &id,
                                               RenderQueueModel::DispatchTarget target,
                                               const QString &workerId)
{
    if (m_queueProcessing && m_activeQueuedJob && m_activeQueuedJob->id == id) {
        return false;
    }
    return m_queue->update(id, [this, target, workerId](RenderQueueModel::QueuedRender &job) {
        configureRemoteFields(job, target, workerId);
    });
}

void RenderController::startNextQueuedRender()
{
    if (!m_queueProcessing) {
        return;
    }

    auto nextJob = m_queue->takeNext();
    if (!nextJob) {
        m_activeQueuedJob.reset();
        m_queueProcessing = false;
        emit queueProcessingChanged(false);
        emit queueDrained();
        return;
    }

    m_activeQueuedJob = nextJob;
    if (m_activeQueuedJob && m_activeQueuedJob->target == RenderQueueModel::DispatchTarget::Remote) {
        const QString workerLabel = m_activeQueuedJob->remoteWorkerLabel.isEmpty()
            ? tr("auto-select")
            : m_activeQueuedJob->remoteWorkerLabel;
        m_activeQueuedJob->remoteStatus = tr("Dispatching to %1").arg(workerLabel);
    }
    emit activeQueueJobChanged(*m_activeQueuedJob);
    if (nextJob->target == RenderQueueModel::DispatchTarget::Remote) {
        startRemoteRender(*nextJob);
    } else {
        startLocalRender(*nextJob);
    }
}

void RenderController::handleServiceRenderFinished()
{
    if (m_queueProcessing && m_activeQueuedJob) {
        if (!m_lastFrame.isNull()) {
            saveFrameForJob(*m_activeQueuedJob, m_lastFrame);
        }
        m_lastFrame = QImage();
    }

    m_activeQueuedJob.reset();
    if (m_queueProcessing) {
        startNextQueuedRender();
    }
}

void RenderController::startLocalRender(const RenderQueueModel::QueuedRender &job)
{
    RendererService::RenderRequest request;
    request.width = job.width;
    request.height = job.height;
    request.samplesPerPixel = job.samples;
    request.scenePath = job.scenePath;
    request.enableDenoiser = job.denoiserEnabled;
    request.toneMappingId = job.toneMapping;
    request.useBVH = job.useBVH;
    m_renderer->startRender(request);
}

void RenderController::startRemoteRender(const RenderQueueModel::QueuedRender &job)
{
    const auto *workerInfo = findRemoteWorkerById(job.remoteWorkerId);
    if (!workerInfo) {
        emit renderFailed(tr("Unknown remote worker '%1'").arg(job.remoteWorkerId));
        if (m_activeQueuedJob) {
            m_queue->prepend(*m_activeQueuedJob);
            m_activeQueuedJob.reset();
        }
        m_queueProcessing = false;
        emit queueProcessingChanged(false);
        return;
    }

    QString error;
    if (!m_remoteManager || !m_remoteManager->startJob(job, workerInfo->definition, &error)) {
        const QString message = error.isEmpty() ? tr("Unable to start remote job") : error;
        emit renderFailed(message);
        if (m_activeQueuedJob) {
            m_queue->prepend(*m_activeQueuedJob);
            m_activeQueuedJob.reset();
        }
        m_queueProcessing = false;
        emit queueProcessingChanged(false);
        return;
    }

    emit renderStageChanged(tr("Dispatching remote job to %1").arg(workerInfo->definition.displayName));
}

QString RenderController::defaultOutputPathForJob(const RenderQueueModel::QueuedRender &job) const
{
    QFileInfo sceneInfo(job.scenePath);
    const QString sceneBase = sceneInfo.completeBaseName().isEmpty() ? QStringLiteral("scene") : sceneInfo.completeBaseName();
    QString presetSafe = job.presetLabel.isEmpty() ? QStringLiteral("Custom") : job.presetLabel;
    presetSafe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));

    QDir baseDir(QDir::current());
    const QString relativeDir = QStringLiteral("renders");
    if (!baseDir.exists(relativeDir)) {
        baseDir.mkpath(relativeDir);
    }
    const QString fileName = QStringLiteral("%1_%2_%3.png").arg(sceneBase, presetSafe, timestamp);
    return baseDir.filePath(relativeDir + QLatin1Char('/') + fileName);
}

bool RenderController::saveFrameForJob(const RenderQueueModel::QueuedRender &job, const QImage &frame) const
{
    if (job.outputPath.isEmpty() || frame.isNull()) {
        return false;
    }

    QFileInfo outputInfo(job.outputPath);
    QDir dir = outputInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    QString path = outputInfo.absoluteFilePath();
    QString format = outputInfo.suffix().isEmpty() ? QStringLiteral("png") : outputInfo.suffix();
    if (format.compare(QStringLiteral("ppm"), Qt::CaseInsensitive) == 0) {
        // QImage::save handles PPM as "PPM".
        if (frame.save(path, "PPM")) {
            return true;
        }
        return false;
    }

    if (frame.save(path)) {
        return true;
    }

    // Attempt to force format if extension wasn't recognized.
    return frame.save(path, "PNG");
}

void RenderController::refreshRemoteWorkers()
{
    m_remoteWorkers.clear();
    if (!m_remoteConfig) {
        emit remoteWorkersChanged();
        return;
    }

    for (const auto &definition : m_remoteConfig->workers()) {
        RemoteWorkerInfo info;
        info.definition = definition;
        info.available = definition.enabled;
        info.statusLabel = definition.enabled ? tr("Ready") : tr("Disabled");
        m_remoteWorkers.append(info);
    }
    emit remoteWorkersChanged();
}

QString RenderController::normalizedRemoteWorkerId(const QString &workerId) const
{
    const QString trimmed = workerId.trimmed();
    if (trimmed.isEmpty()) {
        if (m_remoteConfig && !m_remoteConfig->defaultWorkerId().isEmpty()) {
            return m_remoteConfig->defaultWorkerId();
        }
        return m_remoteWorkers.isEmpty() ? QString() : m_remoteWorkers.front().definition.id;
    }
    for (const auto &worker : m_remoteWorkers) {
        if (worker.definition.id.compare(trimmed, Qt::CaseInsensitive) == 0) {
            return worker.definition.id;
        }
    }
    if (m_remoteConfig && !m_remoteConfig->defaultWorkerId().isEmpty()) {
        return m_remoteConfig->defaultWorkerId();
    }
    return m_remoteWorkers.isEmpty() ? QString() : m_remoteWorkers.front().definition.id;
}

QString RenderController::remoteWorkerLabelForId(const QString &workerId) const
{
    const QString normalized = normalizedRemoteWorkerId(workerId);
    for (const auto &worker : m_remoteWorkers) {
        if (worker.definition.id == normalized) {
            return worker.definition.displayName;
        }
    }
    return QString();
}

const RenderController::RemoteWorkerInfo *RenderController::findRemoteWorkerById(const QString &workerId) const
{
    const QString normalized = normalizedRemoteWorkerId(workerId);
    for (const auto &worker : m_remoteWorkers) {
        if (worker.definition.id == normalized) {
            return &worker;
        }
    }
    return nullptr;
}

void RenderController::updateWorkerActivity(const QString &workerId, int activeDelta)
{
    const QString normalized = normalizedRemoteWorkerId(workerId);
    bool changed = false;
    for (auto &worker : m_remoteWorkers) {
        if (worker.definition.id == normalized) {
            worker.activeJobs = std::max(0, worker.activeJobs + activeDelta);
            if (!worker.available) {
                worker.statusLabel = tr("Offline");
            } else if (worker.activeJobs >= worker.definition.maxConcurrency && worker.definition.maxConcurrency > 0) {
                worker.statusLabel = tr("At capacity");
            } else if (worker.activeJobs > 0) {
                worker.statusLabel = tr("Running %1 job(s)").arg(worker.activeJobs);
            } else {
                worker.statusLabel = tr("Ready");
            }
            changed = true;
            break;
        }
    }
    if (changed) {
        emit remoteWorkersChanged();
    }
}

void RenderController::configureRemoteFields(RenderQueueModel::QueuedRender &job,
                                            RenderQueueModel::DispatchTarget target,
                                            const QString &workerId) const
{
    if (target != RenderQueueModel::DispatchTarget::Remote) {
        job.target = RenderQueueModel::DispatchTarget::Local;
        job.remoteWorkerId.clear();
        job.remoteWorkerLabel.clear();
        job.remoteStatus.clear();
        job.remoteJobId.clear();
        job.remoteArtifact.clear();
        return;
    }

    job.target = RenderQueueModel::DispatchTarget::Remote;
    QString resolvedWorkerId = workerId.isEmpty() ? job.remoteWorkerId : workerId;
    resolvedWorkerId = normalizedRemoteWorkerId(resolvedWorkerId);
    if (resolvedWorkerId.isEmpty() && m_remoteConfig) {
        resolvedWorkerId = m_remoteConfig->defaultWorkerId();
    }
    job.remoteWorkerId = resolvedWorkerId;
    job.remoteWorkerLabel = remoteWorkerLabelForId(job.remoteWorkerId);

    const QString workerLabel = job.remoteWorkerLabel.isEmpty()
        ? tr("auto-select")
        : job.remoteWorkerLabel;
    job.remoteStatus = tr("Queued for %1").arg(workerLabel);
    job.remoteJobId.clear();
    job.remoteArtifact.clear();
}

void RenderController::applyRemoteSettings(RenderQueueModel::QueuedRender &job) const
{
    const auto target = m_settings.remoteRenderingEnabled()
        ? RenderQueueModel::DispatchTarget::Remote
        : RenderQueueModel::DispatchTarget::Local;
    configureRemoteFields(job, target, m_settings.remoteWorkerId());
}

void RenderController::handleRemoteJobStarted(const QString &jobId)
{
    emit renderStarted();
    if (m_activeQueuedJob) {
        if (!m_activeQueuedJob->remoteWorkerId.isEmpty()) {
            updateWorkerActivity(m_activeQueuedJob->remoteWorkerId, +1);
        }
        m_activeQueuedJob->remoteJobId = jobId;
        m_activeQueuedJob->remoteStatus = tr("Remote job %1 started").arg(jobId);
        emit activeQueueJobChanged(*m_activeQueuedJob);
    }
}

void RenderController::handleRemoteJobProgress(const QString &jobId, double value, const QString &label)
{
    Q_UNUSED(jobId);
    emit progressUpdated(value, -1.0);
    if (!label.isEmpty()) {
        emit renderStageChanged(label);
    }
    if (m_activeQueuedJob) {
        m_activeQueuedJob->remoteStatus = label;
        emit activeQueueJobChanged(*m_activeQueuedJob);
    }
}

void RenderController::handleRemoteJobCompleted(const RemoteDispatchResult &result)
{
    if (!result.downloadedFile.isEmpty()) {
        QImage frame(result.downloadedFile);
        if (!frame.isNull()) {
            m_lastFrame = frame;
            emit frameReady(frame);
        }
    }
    if (m_activeQueuedJob) {
        m_activeQueuedJob->remoteStatus = tr("Remote job complete");
        m_activeQueuedJob->remoteArtifact = result.remoteArtifactPath;
        if (!m_activeQueuedJob->remoteWorkerId.isEmpty()) {
            updateWorkerActivity(m_activeQueuedJob->remoteWorkerId, -1);
        }
        emit activeQueueJobChanged(*m_activeQueuedJob);
    }
    emit renderStageChanged(tr("Remote download complete"));
    emit renderFinished();
    handleServiceRenderFinished();
}

void RenderController::handleRemoteJobFailed(const QString &jobId, const QString &message)
{
    Q_UNUSED(jobId);
    emit renderFailed(message);
    if (m_activeQueuedJob) {
        m_activeQueuedJob->remoteStatus = tr("Remote job failed: %1").arg(message);
        if (!m_activeQueuedJob->remoteWorkerId.isEmpty()) {
            updateWorkerActivity(m_activeQueuedJob->remoteWorkerId, -1);
        }
        m_queue->prepend(*m_activeQueuedJob);
        m_activeQueuedJob.reset();
    }
    if (m_queueProcessing) {
        m_queueProcessing = false;
        emit queueProcessingChanged(false);
    }
}

} // namespace ui
} // namespace Raytracer
