#pragma once

#include <QJsonDocument>
#include <QProcess>
#include <QStringList>

#include "ui/services/remote/RemoteDispatchClient.h"

namespace Raytracer {
namespace ui {

class SshDispatchClient : public RemoteDispatchClient
{
    Q_OBJECT

public:
    explicit SshDispatchClient(QObject *parent = nullptr);

    void start(const RemoteWorkerDefinition &worker,
               const RemoteDispatchPayload &payload) override;
    void cancel() override;

private slots:
    void handleStdout();
    void handleStderr();
    void handleFinished(int exitCode, QProcess::ExitStatus status);

private:
    QStringList buildSshArgs(const RemoteWorkerDefinition &worker) const;
    QStringList buildScpArgs(const RemoteWorkerDefinition &worker, const QString &remotePath, const QString &localPath) const;
    QJsonDocument buildJobDocument(const RemoteDispatchPayload &payload) const;
    bool downloadRemoteArtifact(const QString &remotePath, QString *error);

    RemoteWorkerDefinition m_worker;
    RemoteDispatchPayload m_payload;
    QString m_remoteResultPath;
    std::unique_ptr<QProcess> m_process;
    bool m_running {false};
};

} // namespace ui
} // namespace Raytracer
