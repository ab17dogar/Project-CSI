#include "ui/services/remote/RemoteCredentialsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <string>

#ifdef Q_OS_MAC
#include <Security/Security.h>
#endif

namespace Raytracer {
namespace ui {

namespace {
constexpr auto kServiceName = "raytracer.remote.dispatch";
}

RemoteCredentialsStore::RemoteCredentialsStore(QObject *parent)
    : QObject(parent)
{
}

QString RemoteCredentialsStore::fetchToken(const QString &key, QString *error) const
{
#ifdef Q_OS_MAC
    return readFromKeychain(key, error);
#else
    return readFromFallback(key, error);
#endif
}

bool RemoteCredentialsStore::storeToken(const QString &key, const QString &token, QString *error)
{
#ifdef Q_OS_MAC
    return writeToKeychain(key, token, error);
#else
    return writeToFallback(key, token, error);
#endif
}

bool RemoteCredentialsStore::removeToken(const QString &key, QString *error)
{
#ifdef Q_OS_MAC
    return deleteFromKeychain(key, error);
#else
    return deleteFromFallback(key, error);
#endif
}

#ifdef Q_OS_MAC

QString RemoteCredentialsStore::readFromKeychain(const QString &key, QString *error) const
{
    QByteArray serviceBytes = QByteArray::fromStdString(std::string(kServiceName));
    QByteArray accountBytes = key.toUtf8();
    void *passwordData = nullptr;
    UInt32 passwordLength = 0;
    SecKeychainItemRef itemRef = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr,
                                                           static_cast<UInt32>(serviceBytes.size()), serviceBytes.constData(),
                                                           static_cast<UInt32>(accountBytes.size()), accountBytes.constData(),
                                                           &passwordLength, &passwordData, &itemRef);
    if (status == errSecItemNotFound) {
        if (error) {
            *error = tr("No token stored for '%1'").arg(key);
        }
        return {};
    }

    if (status != errSecSuccess) {
        if (error) {
            *error = tr("Keychain error %1 while reading '%2'").arg(status).arg(key);
        }
        return {};
    }

    const QByteArray tokenBytes(static_cast<const char *>(passwordData), static_cast<int>(passwordLength));
    SecKeychainItemFreeContent(nullptr, passwordData);
    if (itemRef) {
        CFRelease(itemRef);
    }
    return QString::fromUtf8(tokenBytes);
}

bool RemoteCredentialsStore::writeToKeychain(const QString &key, const QString &token, QString *error)
{
    QByteArray serviceBytes = QByteArray::fromStdString(std::string(kServiceName));
    QByteArray accountBytes = key.toUtf8();
    QByteArray tokenBytes = token.toUtf8();

    SecKeychainItemRef itemRef = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(nullptr,
                                                     static_cast<UInt32>(serviceBytes.size()), serviceBytes.constData(),
                                                     static_cast<UInt32>(accountBytes.size()), accountBytes.constData(),
                                                     nullptr, nullptr, &itemRef);
    if (status == errSecItemNotFound) {
        status = SecKeychainAddGenericPassword(nullptr,
                                               static_cast<UInt32>(serviceBytes.size()), serviceBytes.constData(),
                                               static_cast<UInt32>(accountBytes.size()), accountBytes.constData(),
                                               static_cast<UInt32>(tokenBytes.size()), tokenBytes.constData(),
                                               nullptr);
        if (status != errSecSuccess && error) {
            *error = tr("Failed to store token '%1' (error %2)").arg(key).arg(status);
        }
        return status == errSecSuccess;
    }

    if (status != errSecSuccess) {
        if (error) {
            *error = tr("Keychain error %1 while locating '%2'").arg(status).arg(key);
        }
        return false;
    }

    status = SecKeychainItemModifyAttributesAndData(itemRef, nullptr,
                                                    static_cast<UInt32>(tokenBytes.size()), tokenBytes.constData());
    CFRelease(itemRef);
    if (status != errSecSuccess && error) {
        *error = tr("Failed to update token '%1' (error %2)").arg(key).arg(status);
    }
    return status == errSecSuccess;
}

bool RemoteCredentialsStore::deleteFromKeychain(const QString &key, QString *error)
{
    QByteArray serviceBytes = QByteArray::fromStdString(std::string(kServiceName));
    QByteArray accountBytes = key.toUtf8();
    SecKeychainItemRef itemRef = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(nullptr,
                                                           static_cast<UInt32>(serviceBytes.size()), serviceBytes.constData(),
                                                           static_cast<UInt32>(accountBytes.size()), accountBytes.constData(),
                                                           nullptr, nullptr, &itemRef);
    if (status == errSecItemNotFound) {
        return true;
    }
    if (status != errSecSuccess) {
        if (error) {
            *error = tr("Keychain error %1 while removing '%2'").arg(status).arg(key);
        }
        return false;
    }
    const OSStatus deleteStatus = SecKeychainItemDelete(itemRef);
    CFRelease(itemRef);
    if (deleteStatus != errSecSuccess && error) {
        *error = tr("Failed to delete token '%1' (error %2)").arg(key).arg(deleteStatus);
    }
    return deleteStatus == errSecSuccess;
}

#else

QString RemoteCredentialsStore::fallbackPath() const
{
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir dir(configDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    return dir.filePath(QStringLiteral("remote_credentials.json"));
}

void RemoteCredentialsStore::ensureFallbackDir(const QString &path) const
{
    QFileInfo info(path);
    QDir dir = info.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
}

QString RemoteCredentialsStore::readFromFallback(const QString &key, QString *error) const
{
    QFile file(fallbackPath());
    if (!file.exists()) {
        if (error) {
            *error = tr("Fallback credential store missing");
        }
        return {};
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to open fallback credentials file");
        }
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    const QString token = doc.object().value(key).toString();
    return token;
}

bool RemoteCredentialsStore::writeToFallback(const QString &key, const QString &token, QString *error)
{
    ensureFallbackDir(fallbackPath());

    QJsonObject data;
    QFile file(fallbackPath());
    if (file.exists()) {
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            data = doc.object();
            file.close();
        }
    }

    data.insert(key, token);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to write fallback credentials file");
        }
        return false;
    }
    file.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    return true;
}

bool RemoteCredentialsStore::deleteFromFallback(const QString &key, QString *error)
{
    QFile file(fallbackPath());
    if (!file.exists()) {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to open fallback credentials file");
        }
        return false;
    }
    QJsonObject data = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    data.remove(key);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        if (error) {
            *error = tr("Unable to rewrite fallback credentials file");
        }
        return false;
    }
    file.write(QJsonDocument(data).toJson(QJsonDocument::Indented));
    return true;
}

#endif

} // namespace ui
} // namespace Raytracer
