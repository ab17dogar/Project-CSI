#include "ui/services/remote/HttpDispatchClient.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QSslError>

namespace Raytracer {
namespace ui {

HttpDispatchClient::HttpDispatchClient(QObject *parent)
    : RemoteDispatchClient(parent)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &HttpDispatchClient::handleStatusPoll);
}

void HttpDispatchClient::start(const RemoteWorkerDefinition &worker,
                               const RemoteDispatchPayload &payload)
{
    m_pollTimer.stop();
    if (m_submitReply) {
        m_submitReply->abort();
    }
    if (m_statusReply) {
        m_statusReply->abort();
    }
    if (m_resultReply) {
        m_resultReply->abort();
    }
    m_worker = worker;
    m_payload = payload;
    m_activeJobId = payload.jobId;
    m_statusUrl.clear();
    m_resultUrl.clear();
    m_remoteArtifactPath.clear();

    const QUrl submitUrl = resolveEndpoint(worker.baseUrl, QString(), QString());
    if (!submitUrl.isValid()) {
        emit failed(payload.jobId, tr("Invalid HTTP endpoint for worker %1").arg(worker.displayName));
        return;
    }

    QNetworkRequest request = makeRequest(submitUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    const QJsonDocument doc = buildJobDocument(payload);
    m_submitReply = m_network.post(request, doc.toJson(QJsonDocument::Compact));
    if (worker.allowSelfSigned && m_submitReply) {
        QNetworkReply *reply = m_submitReply;
        connect(m_submitReply, &QNetworkReply::sslErrors, this,
                [reply](const QList<QSslError> &errors) {
                    Q_UNUSED(errors);
                    if (reply) {
                        reply->ignoreSslErrors();
                    }
                });
    }
    connect(m_submitReply, &QNetworkReply::finished, this, &HttpDispatchClient::handleSubmitFinished);
}

void HttpDispatchClient::cancel()
{
    m_pollTimer.stop();
    if (m_submitReply) {
        m_submitReply->abort();
    }
    if (m_resultReply) {
        m_resultReply->abort();
    }
}

void HttpDispatchClient::handleSubmitFinished()
{
    if (!m_submitReply) {
        return;
    }

    const auto reply = m_submitReply;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(m_payload.jobId, reply->errorString());
        m_submitReply = nullptr;
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    const QJsonObject root = doc.object();
    m_activeJobId = root.value(QStringLiteral("jobId")).toString(m_payload.jobId);
    m_statusUrl = root.value(QStringLiteral("statusUrl")).toString();
    m_resultUrl = root.value(QStringLiteral("resultUrl")).toString();
    m_remoteArtifactPath = root.value(QStringLiteral("remotePath")).toString();
    m_pollIntervalMs = root.value(QStringLiteral("pollIntervalMs")).toInt(2000);

    if (m_statusUrl.isEmpty()) {
        const QUrl status = resolveEndpoint(m_worker.baseUrl, m_worker.statusEndpoint, m_activeJobId);
        m_statusUrl = status.toString();
    }
    if (m_resultUrl.isEmpty()) {
        const QUrl result = resolveEndpoint(m_worker.baseUrl, m_worker.resultEndpoint, m_activeJobId);
        m_resultUrl = result.toString();
    }

    emit started(m_activeJobId);
    requestStatus();
    m_submitReply = nullptr;
}

void HttpDispatchClient::handleStatusPoll()
{
    requestStatus();
}

void HttpDispatchClient::handleResultDownloaded()
{
    if (!m_resultReply) {
        return;
    }

    const auto reply = m_resultReply;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit failed(m_activeJobId, reply->errorString());
        m_resultReply = nullptr;
        return;
    }

    QByteArray data = reply->readAll();
    QFileInfo outputInfo(m_payload.outputPath);
    if (!outputInfo.dir().exists()) {
        outputInfo.dir().mkpath(QStringLiteral("."));
    }
    QFile outFile(m_payload.outputPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit failed(m_activeJobId, tr("Unable to write %1").arg(m_payload.outputPath));
        m_resultReply = nullptr;
        return;
    }
    outFile.write(data);
    outFile.close();

    RemoteDispatchResult result;
    result.jobId = m_activeJobId;
    result.remoteArtifactPath = m_remoteArtifactPath;
    result.downloadedFile = m_payload.outputPath;
    result.imageData = data;
    emit completed(result);
    m_resultReply = nullptr;
}

QJsonDocument HttpDispatchClient::buildJobDocument(const RemoteDispatchPayload &payload) const
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

    QJsonObject metaObj;
    for (auto it = payload.metadata.cbegin(); it != payload.metadata.cend(); ++it) {
        metaObj.insert(it.key(), it.value());
    }
    root.insert(QStringLiteral("metadata"), metaObj);
    return QJsonDocument(root);
}

void HttpDispatchClient::requestStatus()
{
    if (m_statusUrl.isEmpty()) {
        emit failed(m_activeJobId, tr("Missing status URL for HTTP worker"));
        m_pollTimer.stop();
        return;
    }

    if (m_statusReply) {
        return;
    }

    QNetworkRequest request = makeRequest(QUrl(m_statusUrl));
    QNetworkReply *reply = m_network.get(request);
    m_statusReply = reply;
    if (m_worker.allowSelfSigned) {
        connect(reply, &QNetworkReply::sslErrors, this,
                [reply](const QList<QSslError> &errors) {
                    Q_UNUSED(errors);
                    if (reply) {
                        reply->ignoreSslErrors();
                    }
                });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_statusReply = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(m_activeJobId, reply->errorString());
            m_pollTimer.stop();
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonObject root = doc.object();
        const QString state = root.value(QStringLiteral("state")).toString(QStringLiteral("unknown"));
        const double progressValue = root.value(QStringLiteral("progress")).toDouble(0.0);
        const QString label = root.value(QStringLiteral("label")).toString();
        emit progress(m_activeJobId, progressValue, label);

        if (state.compare(QStringLiteral("completed"), Qt::CaseInsensitive) == 0) {
            m_pollTimer.stop();
            const QString remotePath = root.value(QStringLiteral("remotePath")).toString();
            if (!remotePath.isEmpty()) {
                m_remoteArtifactPath = remotePath;
            }
            downloadResult();
        } else if (state.compare(QStringLiteral("failed"), Qt::CaseInsensitive) == 0) {
            m_pollTimer.stop();
            const QString message = root.value(QStringLiteral("message")).toString(tr("Remote job failed"));
            emit failed(m_activeJobId, message);
        } else {
            m_pollTimer.start(m_pollIntervalMs);
        }
    });
}

void HttpDispatchClient::downloadResult()
{
    if (m_resultUrl.isEmpty()) {
        emit failed(m_activeJobId, tr("Missing result URL for HTTP worker"));
        return;
    }

    QNetworkRequest request = makeRequest(QUrl(m_resultUrl));
    m_resultReply = m_network.get(request);
    if (m_worker.allowSelfSigned && m_resultReply) {
        QNetworkReply *reply = m_resultReply;
        connect(m_resultReply, &QNetworkReply::sslErrors, this,
                [reply](const QList<QSslError> &errors) {
                    Q_UNUSED(errors);
                    if (reply) {
                        reply->ignoreSslErrors();
                    }
                });
    }
    connect(m_resultReply, &QNetworkReply::finished, this, &HttpDispatchClient::handleResultDownloaded);
}

QNetworkRequest HttpDispatchClient::makeRequest(const QUrl &url) const
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    if (!m_payload.authToken.isEmpty()) {
        const QByteArray tokenBytes = QByteArrayLiteral("Bearer ") + m_payload.authToken.toUtf8();
        request.setRawHeader("Authorization", tokenBytes);
    }
    return request;
}

QUrl HttpDispatchClient::resolveEndpoint(const QString &base, const QString &endpoint, const QString &jobId) const
{
    QUrl url(base);
    QString path = url.path();
    if (!endpoint.isEmpty()) {
        QString resolved = endpoint;
        if (!jobId.isEmpty()) {
            resolved.replace(QStringLiteral("{id}"), jobId);
        }
        if (!resolved.startsWith('/')) {
            if (!path.endsWith('/')) {
                path.append('/');
            }
            path.append(resolved);
        } else {
            path = resolved;
        }
        url.setPath(path);
    }
    return url;
}

} // namespace ui
} // namespace Raytracer
