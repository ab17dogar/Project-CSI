#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDir>

namespace Raytracer {
namespace ui {

class SceneImportService : public QObject
{
    Q_OBJECT

public:
    struct ImportResult {
        bool success {false};
        QString importedScenePath;
        QString sceneName;
        QStringList copiedFiles;
        QStringList missingFiles;
        QString error;
    };

    explicit SceneImportService(QObject *parent = nullptr);

    ImportResult importScene(const QString &sourceXmlPath,
                             const QString &targetAssetsRoot = QString());
    ImportResult importMeshScene(const QString &sourceObjPath,
                                 const QString &targetAssetsRoot = QString());

private:
    QString locateAssetsRoot() const;
    QString resolveSourcePath(const QString &reference,
                              const QDir &sceneDir) const;
    void copyAssociatedMaterials(const QString &sourceObjPath,
                                 const QString &assetsRoot,
                                 QStringList *copiedFiles,
                                 QStringList *missingFiles) const;
};

} // namespace ui
} // namespace Raytracer
