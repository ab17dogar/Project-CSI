#include "ui/services/remote/SshDispatchClient.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QTemporaryFile>
#include <QTextStream>
#include <QUuid>

namespace Raytracer {
namespace ui {

SshDispatchClient::SshDispatchClient(QObject *parent)
    : RemoteDispatchClient(parent)
{
}

void SshDispatchClient::start(const RemoteWorkerDefinition &worker,
                              const RemoteDispatchPayload &payload)
{
    if (m_running) {
        emit failed(payload.jobId, tr("SSH client already running"));
        return;
    }

    m_worker = worker;
    m_payload = payload;
    m_remoteResultPath.clear();

    m_process = std::make_unique<QProcess>(this);
    QStringList args = buildSshArgs(worker);
    QString remoteCmd = worker.remoteExecutable.isEmpty()
        ? QStringLiteral("raytracer_worker --stdin")
        : worker.remoteExecutable;
    if (!worker.stagingPath.isEmpty()) {
        remoteCmd.append(QStringLiteral(" --staging %1").arg(worker.stagingPath));
    }
    remoteCmd.append(QStringLiteral(" --job-id %1").arg(payload.jobId));
    args << remoteCmd;

    connect(m_process.get(), &QProcess::readyReadStandardOutput,
            this, &SshDispatchClient::handleStdout);
    connect(m_process.get(), &QProcess::readyReadStandardError,
            this, &SshDispatchClient::handleStderr);
    connect(m_process.get(), qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SshDispatchClient::handleFinished);

    m_process->start(QStringLiteral("ssh"), args);
    if (!m_process->waitForStarted()) {
        emit failed(payload.jobId, tr("Failed to start ssh process"));
        m_process.reset();
        return;
    }

    emit started(payload.jobId);
    m_running = true;

    const QJsonDocument doc = buildJobDocument(payload);
    m_process->write(doc.toJson(QJsonDocument::Compact));
    m_process->closeWriteChannel();
}

void SshDispatchClient::cancel()
{
    if (!m_process) {
        return;
    }
    m_process->kill();
}

void SshDispatchClient::handleStdout()
{
    if (!m_process) {
        return;
    }
    while (m_process->canReadLine()) {
        const QByteArray line = m_process->readLine().trimmed();
        if (line.startsWith("PROGRESS")) {
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() >= 3) {
                bool ok = false;
                const double value = parts.at(1).toDouble(&ok);
                const QString label = QString::fromUtf8(parts.mid(2).join(' '));
                if (ok) {
                    emit progress(m_payload.jobId, value, label);
                }
            }
        } else if (line.startsWith("RESULT")) {
            const QString path = QString::fromUtf8(line.mid(6)).trimmed();
            if (!path.isEmpty()) {
                m_remoteResultPath = path;
            }
        } else if (line.startsWith("ERROR")) {
            const QString message = QString::fromUtf8(line.mid(5)).trimmed();
            emit failed(m_payload.jobId, message);
        }
    }
}

void SshDispatchClient::handleStderr()
{
    if (!m_process) {
        return;
    }
    const QByteArray data = m_process->readAllStandardError();
    if (data.isEmpty()) {
        return;
    }
    const QList<QByteArray> lines = data.split('\n');
    for (const auto &rawLine : lines) {
        const QByteArray line = rawLine.trimmed();
        if (!line.isEmpty()) {
            emit progress(m_payload.jobId, 0.0, QString::fromUtf8(line));
        }
    }
}

void SshDispatchClient::handleFinished(int exitCode, QProcess::ExitStatus status)
{
    m_running = false;
    if (!m_process) {
        return;
    }

    if (status != QProcess::NormalExit || exitCode != 0) {
        emit failed(m_payload.jobId, tr("SSH command exited with code %1").arg(exitCode));
        m_process.reset();
        return;
    }

    if (m_remoteResultPath.isEmpty()) {
        emit failed(m_payload.jobId, tr("Remote worker did not report an output path"));
        m_process.reset();
        return;
    }

    QString error;
    if (!downloadRemoteArtifact(m_remoteResultPath, &error)) {
        emit failed(m_payload.jobId, error);
        m_process.reset();
        return;
    }

    RemoteDispatchResult result;
    result.jobId = m_payload.jobId;
    result.remoteArtifactPath = m_remoteResultPath;
    result.downloadedFile = m_payload.outputPath;
    emit completed(result);
    m_process.reset();
}

QStringList SshDispatchClient::buildSshArgs(const RemoteWorkerDefinition &worker) const
{
    QStringList args;
    if (worker.port > 0) {
        args << QStringLiteral("-p") << QString::number(worker.port);
    }
    if (!worker.identityFile.isEmpty()) {
        args << QStringLiteral("-i") << worker.identityFile;
    }
    const QString remoteHost = worker.sshUser.isEmpty()
        ? worker.host
        : QStringLiteral("%1@%2").arg(worker.sshUser, worker.host);
    args << remoteHost;
    return args;
}

QStringList SshDispatchClient::buildScpArgs(const RemoteWorkerDefinition &worker, const QString &remotePath, const QString &localPath) const
{
    QStringList args;
    if (worker.port > 0) {
        args << QStringLiteral("-P") << QString::number(worker.port);
    }
    if (!worker.identityFile.isEmpty()) {
        args << QStringLiteral("-i") << worker.identityFile;
    }
    const QString remoteHost = worker.sshUser.isEmpty()
        ? worker.host
        : QStringLiteral("%1@%2").arg(worker.sshUser, worker.host);
    args << QStringLiteral("%1:%2").arg(remoteHost, remotePath);
    args << localPath;
    return args;
}

QJsonDocument SshDispatchClient::buildJobDocument(const RemoteDispatchPayload &payload) const
{
    QJsonObject root;
    root.insert(QStringLiteral("jobId"), payload.jobId);
    root.insert(QStringLiteral("width"), payload.width);
    root.insert(QStringLiteral("height"), payload.height);
    root.insert(QStringLiteral("samplesPerPixel"), payload.samplesPerPixel);
    root.insert(QStringLiteral("denoiser"), payload.enableDenoiser);
    root.insert(QStringLiteral("toneMapping"), payload.toneMappingId);
    root.insert(QStringLiteral("outputFormat"), payload.outputFormat);

    QJsonObject sceneObj;
    sceneObj.insert(QStringLiteral("fileName"), payload.sceneFileName);
    sceneObj.insert(QStringLiteral("contents"), QString::fromLatin1(payload.sceneContents.toBase64()));
    root.insert(QStringLiteral("scene"), sceneObj);

    QJsonObject metadata;
    for (auto it = payload.metadata.cbegin(); it != payload.metadata.cend(); ++it) {
        metadata.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("metadata"), metadata);
    return QJsonDocument(root);
}

bool SshDispatchClient::downloadRemoteArtifact(const QString &remotePath, QString *error)
{
    if (m_payload.outputPath.isEmpty()) {
        if (error) {
            *error = tr("No output path specified for remote download");
        }
        return false;
    }

    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        if (error) {
            *error = tr("Unable to create temporary file for download");
        }
        return false;
    }
    const QString localTempPath = tempFile.fileName();
    tempFile.close();

    QProcess scpProcess;
    const QStringList args = buildScpArgs(m_worker, remotePath, localTempPath);
    scpProcess.start(QStringLiteral("scp"), args);
    if (!scpProcess.waitForFinished(-1) || scpProcess.exitStatus() != QProcess::NormalExit || scpProcess.exitCode() != 0) {
        if (error) {
            *error = tr("scp failed to download %1").arg(remotePath);
        }
        return false;
    }

    QFileInfo info(m_payload.outputPath);
    if (!info.dir().exists()) {
        info.dir().mkpath(QStringLiteral("."));
    }
    QFile::remove(m_payload.outputPath);
    if (!QFile::copy(localTempPath, m_payload.outputPath)) {
        if (error) {
            *error = tr("Unable to copy downloaded artifact to %1").arg(m_payload.outputPath);
        }
        return false;
    }

    QFile::remove(localTempPath);
    return true;
}

} // namespace ui
} // namespace Raytracer
