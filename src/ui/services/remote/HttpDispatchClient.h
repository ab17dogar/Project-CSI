#pragma once

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>

#include "ui/services/remote/RemoteDispatchClient.h"

namespace Raytracer {
namespace ui {

class HttpDispatchClient : public RemoteDispatchClient
{
    Q_OBJECT

public:
    explicit HttpDispatchClient(QObject *parent = nullptr);

    void start(const RemoteWorkerDefinition &worker,
               const RemoteDispatchPayload &payload) override;
    void cancel() override;

private slots:
    void handleSubmitFinished();
    void handleStatusPoll();
    void handleResultDownloaded();

private:
    QJsonDocument buildJobDocument(const RemoteDispatchPayload &payload) const;
    void requestStatus();
    void downloadResult();
    void resetReplies();
    QNetworkRequest makeRequest(const QUrl &url) const;
    QUrl resolveEndpoint(const QString &base, const QString &endpoint, const QString &jobId = QString()) const;

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_submitReply;
    QPointer<QNetworkReply> m_statusReply;
    QPointer<QNetworkReply> m_resultReply;
    RemoteWorkerDefinition m_worker;
    RemoteDispatchPayload m_payload;
    QString m_statusUrl;
    QString m_resultUrl;
    QString m_remoteArtifactPath;
    int m_pollIntervalMs {2000};
    QTimer m_pollTimer;
    QString m_activeJobId;
};

} // namespace ui
} // namespace Raytracer
