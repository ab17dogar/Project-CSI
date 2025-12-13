#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QMetaType>
#include <QFileSystemWatcher>
#include <memory>
#include <QJsonObject>

namespace Raytracer {
namespace ui {

enum class RemoteTransportMethod {
    SSH,
    HTTP
};

struct RemoteWorkerDefinition {
    QString id;
    QString displayName;
    RemoteTransportMethod transport {RemoteTransportMethod::SSH};
    QString host;
    quint16 port {0};
    QString sshUser;
    QString identityFile;
    QString remoteExecutable;
    QString stagingPath;
    QString baseUrl;
    QString statusEndpoint;
    QString resultEndpoint;
    int maxConcurrency {1};
    QString credentialKey;
    bool allowSelfSigned {false};
    bool enabled {true};
};

class RemoteWorkerConfigRepository : public QObject
{
    Q_OBJECT

public:
    explicit RemoteWorkerConfigRepository(QObject *parent = nullptr);

    const QVector<RemoteWorkerDefinition> &workers() const noexcept { return m_workers; }
    QString defaultWorkerId() const { return m_defaultWorkerId; }
    QString configPath() const { return m_configPath; }

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;
    bool reload(QString *error = nullptr);

signals:
    void workersChanged();

private:
    void ensureWatcher();
    void handleConfigChanged(const QString &path);
    QString resolveConfigPath() const;
    void ensureConfigDirectory(const QString &path) const;
    bool writeDefaultConfig(QString *error);
    bool parseDocument(const QByteArray &bytes, QString *error);
    QJsonObject serializeWorker(const RemoteWorkerDefinition &worker) const;
    RemoteWorkerDefinition parseWorker(const QJsonObject &object, QString *error) const;
    static QString transportToString(RemoteTransportMethod method);
    static RemoteTransportMethod transportFromString(const QString &value);

    QVector<RemoteWorkerDefinition> m_workers;
    QString m_defaultWorkerId;
    QString m_configPath;
    std::unique_ptr<QFileSystemWatcher> m_watcher;
};

} // namespace ui
} // namespace Raytracer

Q_DECLARE_METATYPE(Raytracer::ui::RemoteWorkerDefinition)
Q_DECLARE_METATYPE(Raytracer::ui::RemoteTransportMethod)
