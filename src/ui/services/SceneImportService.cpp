#include "ui/services/SceneImportService.h"

#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStringList>

namespace Raytracer {
namespace ui {

SceneImportService::SceneImportService(QObject *parent)
    : QObject(parent)
{
}

SceneImportService::ImportResult SceneImportService::importScene(const QString &sourceXmlPath,
                                                                const QString &targetAssetsRoot)
{
    ImportResult result;
    QFileInfo sourceInfo(sourceXmlPath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.error = tr("Scene file does not exist: %1").arg(sourceXmlPath);
        return result;
    }

    QString assetsRoot = targetAssetsRoot;
    if (assetsRoot.isEmpty()) {
        assetsRoot = locateAssetsRoot();
    }

    if (assetsRoot.isEmpty()) {
        result.error = tr("Unable to locate or create an assets directory.");
        return result;
    }

    // Copy XML file directly to assets/
    const QString destXmlPath = QDir(assetsRoot).filePath(sourceInfo.fileName());
    
    QFile xmlFile(sourceInfo.absoluteFilePath());
    if (!xmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = tr("Failed to open %1").arg(sourceInfo.absoluteFilePath());
        return result;
    }

    QDomDocument document;
    QString parseError;
    int errorLine = 0;
    int errorColumn = 0;
    if (!document.setContent(&xmlFile, &parseError, &errorLine, &errorColumn)) {
        result.error = tr("XML parse error at %1:%2 — %3")
            .arg(errorLine)
            .arg(errorColumn)
            .arg(parseError);
        return result;
    }

    xmlFile.close();

    const QDir sceneDir = sourceInfo.dir();
    QStringList copied;
    QStringList missing;

    // Process all File references and copy them to assets/
    QDomNodeList fileNodes = document.elementsByTagName(QStringLiteral("File"));
    for (int i = 0; i < fileNodes.count(); ++i) {
        QDomElement elem = fileNodes.at(i).toElement();
        if (elem.isNull() || !elem.hasAttribute(QStringLiteral("name"))) {
            continue;
        }

        const QString originalReference = elem.attribute(QStringLiteral("name")).trimmed();
        if (originalReference.isEmpty()) {
            continue;
        }

        QString resolvedSource = resolveSourcePath(originalReference, sceneDir);
        if (resolvedSource.isEmpty()) {
            missing << originalReference;
            continue;
        }

        // Get just the filename, copy directly to assets/
        const QString fileName = QFileInfo(resolvedSource).fileName();
        const QString destPath = QDir(assetsRoot).filePath(fileName);
        
        if (QFile::exists(destPath)) {
            QFile::remove(destPath);
        }

        if (!QFile::copy(resolvedSource, destPath)) {
            missing << originalReference;
            continue;
        }

        copied << fileName;
        
        // Update the XML reference to just the filename
        elem.setAttribute(QStringLiteral("name"), fileName);
        
        // Also copy associated MTL files for OBJ meshes
        if (resolvedSource.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive)) {
            copyAssociatedMaterials(resolvedSource, assetsRoot, &copied, &missing);
        }
    }

    // Write the modified XML to assets/
    if (QFile::exists(destXmlPath)) {
        QFile::remove(destXmlPath);
    }
    
    QFile outFile(destXmlPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        result.error = tr("Unable to write %1").arg(destXmlPath);
        return result;
    }
    outFile.write(document.toByteArray(2));
    outFile.close();

    result.importedScenePath = destXmlPath;
    result.sceneName = QFileInfo(destXmlPath).completeBaseName();
    result.copiedFiles = copied;
    result.missingFiles = missing;
    result.success = result.missingFiles.isEmpty();
    if (!result.success) {
        result.error = tr("Some referenced assets could not be imported.");
    }
    return result;
}

SceneImportService::ImportResult SceneImportService::importMeshScene(const QString &sourceObjPath,
                                                                    const QString &targetAssetsRoot)
{
    ImportResult result;
    QFileInfo objInfo(sourceObjPath);
    if (!objInfo.exists() || !objInfo.isFile()) {
        result.error = tr("Mesh file does not exist: %1").arg(sourceObjPath);
        return result;
    }

    QString assetsRoot = targetAssetsRoot;
    if (assetsRoot.isEmpty()) {
        assetsRoot = locateAssetsRoot();
    }

    if (assetsRoot.isEmpty()) {
        result.error = tr("Unable to locate or create an assets directory.");
        return result;
    }

    QStringList copied;
    QStringList missing;

    // Copy OBJ file directly to assets/
    const QString objFileName = objInfo.fileName();
    const QString destObjPath = QDir(assetsRoot).filePath(objFileName);
    
    if (QFile::exists(destObjPath)) {
        QFile::remove(destObjPath);
    }

    if (!QFile::copy(objInfo.absoluteFilePath(), destObjPath)) {
        result.error = tr("Unable to copy mesh %1 to assets").arg(objInfo.absoluteFilePath());
        return result;
    }
    
    copied << objFileName;

    // Copy associated MTL files
    copyAssociatedMaterials(objInfo.absoluteFilePath(), assetsRoot, &copied, &missing);

    // Just return the path to the copied OBJ file (no XML generation)
    result.importedScenePath = destObjPath;
    result.sceneName = objInfo.completeBaseName();
    result.copiedFiles = copied;
    result.missingFiles = missing;
    result.success = true;
    return result;
}

QString SceneImportService::locateAssetsRoot() const
{
    QStringList probeDirs;
    probeDirs << QDir::currentPath();
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        probeDirs << appDir;
        QDir temp(appDir);
        for (int i = 0; i < 4; ++i) {
            temp.cdUp();
            probeDirs << temp.absolutePath();
        }
    }

    for (const QString &basePath : std::as_const(probeDirs)) {
        if (basePath.isEmpty()) {
            continue;
        }
        QDir dir(basePath);
        if (dir.exists(QStringLiteral("assets"))) {
            return dir.filePath(QStringLiteral("assets"));
        }
    }

    // Fallback: create assets in the current directory
    QDir cwd(QDir::currentPath());
    if (!cwd.exists(QStringLiteral("assets"))) {
        if (!cwd.mkpath(QStringLiteral("assets"))) {
            return {};
        }
    }
    return cwd.filePath(QStringLiteral("assets"));
}

QString SceneImportService::resolveSourcePath(const QString &reference,
                                              const QDir &sceneDir) const
{
    QFileInfo info(reference);
    if (info.isAbsolute() && info.exists()) {
        return info.absoluteFilePath();
    }

    QFileInfo relative(sceneDir.filePath(reference));
    if (relative.exists()) {
        return relative.absoluteFilePath();
    }

    const QString baseName = QFileInfo(reference).fileName();
    if (!baseName.isEmpty()) {
        QFileInfo fallback(sceneDir.filePath(baseName));
        if (fallback.exists()) {
            return fallback.absoluteFilePath();
        }
    }

    return {};
}

void SceneImportService::copyAssociatedMaterials(const QString &sourceObjPath,
                                                 const QString &assetsRoot,
                                                 QStringList *copiedFiles,
                                                 QStringList *missingFiles) const
{
    QFile objFile(sourceObjPath);
    if (!objFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    const QFileInfo sourceInfo(sourceObjPath);
    const QDir sourceDir = sourceInfo.dir();

    QSet<QString> processed;

    while (!objFile.atEnd()) {
        const QByteArray rawLine = objFile.readLine().trimmed();
        if (!rawLine.startsWith("mtllib")) {
            continue;
        }

        const QList<QByteArray> parts = QByteArray(rawLine).split(' ');
        if (parts.size() < 2) {
            continue;
        }
        QString mtlReference = QString::fromUtf8(parts.at(1)).trimmed();
        if (mtlReference.isEmpty()) {
            continue;
        }
        if (processed.contains(mtlReference)) {
            continue;
        }
        processed.insert(mtlReference);

        QFileInfo resolvedInfo(mtlReference);
        QString resolvedPath;
        if (resolvedInfo.isAbsolute() && resolvedInfo.exists()) {
            resolvedPath = resolvedInfo.absoluteFilePath();
        } else {
            QFileInfo relativeInfo(sourceDir.filePath(mtlReference));
            if (relativeInfo.exists()) {
                resolvedPath = relativeInfo.absoluteFilePath();
            }
        }
        if (resolvedPath.isEmpty()) {
            if (missingFiles) {
                missingFiles->append(mtlReference);
            }
            continue;
        }

        // Copy MTL file directly to assets/
        const QString mtlFileName = QFileInfo(resolvedPath).fileName();
        const QString destMtlPath = QDir(assetsRoot).filePath(mtlFileName);
        
        if (QFile::exists(destMtlPath)) {
            QFile::remove(destMtlPath);
        }
        
        if (!QFile::copy(resolvedPath, destMtlPath)) {
            if (missingFiles) {
                missingFiles->append(mtlReference);
            }
            continue;
        }
        if (copiedFiles) {
            copiedFiles->append(mtlFileName);
        }
    }
}

} // namespace ui
} // namespace Raytracer
