#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace Raytracer {
namespace ui {

struct RemoteDispatchPayload {
    QString jobId;
    QString scenePath;
    QString sceneFileName;
    QByteArray sceneContents;
    int width {0};
    int height {0};
    int samplesPerPixel {0};
    bool enableDenoiser {false};
    QString toneMappingId;
    QString outputPath;
    QString outputFormat {QStringLiteral("png")};
    QMap<QString, QString> metadata;
    QString authToken;
};

struct RemoteDispatchResult {
    QString jobId;
    QString remoteArtifactPath;
    QString downloadedFile;
    QByteArray imageData;
};

} // namespace ui
} // namespace Raytracer
