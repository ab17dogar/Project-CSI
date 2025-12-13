#pragma once

#include <QObject>
#include <QString>

namespace Raytracer {
namespace ui {

class RemoteCredentialsStore : public QObject
{
    Q_OBJECT

public:
    explicit RemoteCredentialsStore(QObject *parent = nullptr);

    QString fetchToken(const QString &key, QString *error = nullptr) const;
    bool storeToken(const QString &key, const QString &token, QString *error = nullptr);
    bool removeToken(const QString &key, QString *error = nullptr);

private:
#ifdef Q_OS_MAC
    QString readFromKeychain(const QString &key, QString *error) const;
    bool writeToKeychain(const QString &key, const QString &token, QString *error);
    bool deleteFromKeychain(const QString &key, QString *error);
#else
    QString readFromFallback(const QString &key, QString *error) const;
    bool writeToFallback(const QString &key, const QString &token, QString *error);
    bool deleteFromFallback(const QString &key, QString *error);
    QString fallbackPath() const;
    void ensureFallbackDir(const QString &path) const;
#endif
};

} // namespace ui
} // namespace Raytracer
