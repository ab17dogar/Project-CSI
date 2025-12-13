#include "ui/services/remote/RemoteWorkerConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextStream>

namespace Raytracer {
namespace ui {

namespace {
constexpr int kConfigVersion = 1;
constexpr auto kDefaultFileName = "remote_workers.json";
}

RemoteWorkerConfigRepository::RemoteWorkerConfigRepository(QObject *parent)
    : QObject(parent)
{
    m_configPath = resolveConfigPath();
    load(nullptr);
}

bool RemoteWorkerConfigRepository::load(QString *error)
{
    QFile file(m_configPath);
    if (!file.exists()) {
        if (!writeDefaultConfig(error)) {
            return false;
        }
    }

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to open %1 for reading").arg(m_configPath);
        }
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    if (!parseDocument(bytes, error)) {
        return false;
    }

    ensureWatcher();
    emit workersChanged();
    return true;
}

bool RemoteWorkerConfigRepository::reload(QString *error)
{
    return load(error);
}

bool RemoteWorkerConfigRepository::save(QString *error) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kConfigVersion);
    root.insert(QStringLiteral("defaultWorkerId"), m_defaultWorkerId);

    QJsonArray workerArray;
    for (const auto &worker : m_workers) {
        workerArray.append(serializeWorker(worker));
    }
    root.insert(QStringLiteral("workers"), workerArray);

    QJsonDocument doc(root);

    QFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to open %1 for writing").arg(m_configPath);
        }
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void RemoteWorkerConfigRepository::ensureWatcher()
{
    if (!m_watcher) {
        m_watcher = std::make_unique<QFileSystemWatcher>(this);
        connect(m_watcher.get(), &QFileSystemWatcher::fileChanged,
                this, &RemoteWorkerConfigRepository::handleConfigChanged);
    }

    if (!m_watcher->files().contains(m_configPath)) {
        m_watcher->addPath(m_configPath);
    }
}

void RemoteWorkerConfigRepository::handleConfigChanged(const QString &path)
{
    if (path != m_configPath) {
        return;
    }
    load(nullptr);
}

QString RemoteWorkerConfigRepository::resolveConfigPath() const
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(QString::fromUtf8(kDefaultFileName));
}

void RemoteWorkerConfigRepository::ensureConfigDirectory(const QString &path) const
{
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

bool RemoteWorkerConfigRepository::writeDefaultConfig(QString *error)
{
    ensureConfigDirectory(m_configPath);

    RemoteWorkerDefinition worker;
    worker.id = QStringLiteral("example-ssh");
    worker.displayName = tr("Example SSH Worker");
    worker.transport = RemoteTransportMethod::SSH;
    worker.host = QStringLiteral("render.yourstudio.example");
    worker.port = 22;
    worker.sshUser = QStringLiteral("render");
    worker.identityFile = QStringLiteral("~/.ssh/id_rsa");
    worker.remoteExecutable = QStringLiteral("/opt/raytracer/bin/headless_worker");
    worker.stagingPath = QStringLiteral("/tmp/raytracer_jobs");
    worker.maxConcurrency = 1;
    worker.credentialKey = QStringLiteral("example-ssh-token");

    m_workers = {worker};
    m_defaultWorkerId = worker.id;
    return save(error);
}

bool RemoteWorkerConfigRepository::parseDocument(const QByteArray &bytes, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (doc.isNull()) {
        if (error) {
            *error = tr("Config parse error: %1").arg(parseError.errorString());
        }
        return false;
    }

    if (!doc.isObject()) {
        if (error) {
            *error = tr("Config root must be a JSON object");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    m_defaultWorkerId = root.value(QStringLiteral("defaultWorkerId")).toString();

    QVector<RemoteWorkerDefinition> parsed;
    const QJsonArray workersArray = root.value(QStringLiteral("workers")).toArray();
    for (const auto &value : workersArray) {
        if (!value.isObject()) {
            continue;
        }
        QString workerError;
        RemoteWorkerDefinition worker = parseWorker(value.toObject(), &workerError);
        if (!workerError.isEmpty()) {
            if (error) {
                *error = workerError;
            }
            return false;
        }
        parsed.append(worker);
    }

    if (parsed.isEmpty()) {
        if (error) {
            *error = tr("No remote workers defined in %1").arg(m_configPath);
        }
        return false;
    }

    m_workers = parsed;
    if (m_defaultWorkerId.isEmpty()) {
        m_defaultWorkerId = m_workers.front().id;
    }

    return true;
}

QJsonObject RemoteWorkerConfigRepository::serializeWorker(const RemoteWorkerDefinition &worker) const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), worker.id);
    obj.insert(QStringLiteral("displayName"), worker.displayName);
    obj.insert(QStringLiteral("method"), transportToString(worker.transport));
    obj.insert(QStringLiteral("host"), worker.host);
    obj.insert(QStringLiteral("port"), static_cast<int>(worker.port));
    obj.insert(QStringLiteral("sshUser"), worker.sshUser);
    obj.insert(QStringLiteral("identityFile"), worker.identityFile);
    obj.insert(QStringLiteral("remoteExecutable"), worker.remoteExecutable);
    obj.insert(QStringLiteral("stagingPath"), worker.stagingPath);
    obj.insert(QStringLiteral("baseUrl"), worker.baseUrl);
    obj.insert(QStringLiteral("statusEndpoint"), worker.statusEndpoint);
    obj.insert(QStringLiteral("resultEndpoint"), worker.resultEndpoint);
    obj.insert(QStringLiteral("maxConcurrency"), worker.maxConcurrency);
    obj.insert(QStringLiteral("credentialKey"), worker.credentialKey);
    obj.insert(QStringLiteral("allowSelfSigned"), worker.allowSelfSigned);
    obj.insert(QStringLiteral("enabled"), worker.enabled);
    return obj;
}

RemoteWorkerDefinition RemoteWorkerConfigRepository::parseWorker(const QJsonObject &object, QString *error) const
{
    RemoteWorkerDefinition worker;
    worker.id = object.value(QStringLiteral("id")).toString();
    worker.displayName = object.value(QStringLiteral("displayName")).toString(worker.id);
    worker.transport = transportFromString(object.value(QStringLiteral("method")).toString(QStringLiteral("ssh")));
    worker.host = object.value(QStringLiteral("host")).toString();
    worker.port = static_cast<quint16>(object.value(QStringLiteral("port")).toInt(worker.transport == RemoteTransportMethod::SSH ? 22 : 443));
    worker.sshUser = object.value(QStringLiteral("sshUser")).toString();
    worker.identityFile = object.value(QStringLiteral("identityFile")).toString();
    worker.remoteExecutable = object.value(QStringLiteral("remoteExecutable")).toString();
    worker.stagingPath = object.value(QStringLiteral("stagingPath")).toString();
    worker.baseUrl = object.value(QStringLiteral("baseUrl")).toString();
    worker.statusEndpoint = object.value(QStringLiteral("statusEndpoint")).toString(QStringLiteral("/status"));
    worker.resultEndpoint = object.value(QStringLiteral("resultEndpoint")).toString(QStringLiteral("/result"));
    worker.maxConcurrency = object.value(QStringLiteral("maxConcurrency")).toInt(1);
    worker.credentialKey = object.value(QStringLiteral("credentialKey")).toString();
    worker.allowSelfSigned = object.value(QStringLiteral("allowSelfSigned")).toBool(false);
    worker.enabled = object.value(QStringLiteral("enabled")).toBool(true);

    if (worker.id.isEmpty()) {
        if (error) {
            *error = tr("Worker entry missing 'id'");
        }
    }
    if (worker.transport == RemoteTransportMethod::SSH && (worker.host.isEmpty() || worker.sshUser.isEmpty())) {
        if (error) {
            *error = tr("SSH worker '%1' missing host or sshUser").arg(worker.id);
        }
    }
    if (worker.transport == RemoteTransportMethod::HTTP && worker.baseUrl.isEmpty()) {
        if (error) {
            *error = tr("HTTP worker '%1' requires baseUrl").arg(worker.id);
        }
    }

    return worker;
}

QString RemoteWorkerConfigRepository::transportToString(RemoteTransportMethod method)
{
    switch (method) {
    case RemoteTransportMethod::SSH:
        return QStringLiteral("ssh");
    case RemoteTransportMethod::HTTP:
        return QStringLiteral("http");
    }
    return QStringLiteral("ssh");
}

RemoteTransportMethod RemoteWorkerConfigRepository::transportFromString(const QString &value)
{
    if (value.compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
        return RemoteTransportMethod::HTTP;
    }
    return RemoteTransportMethod::SSH;
}

} // namespace ui
} // namespace Raytracer
