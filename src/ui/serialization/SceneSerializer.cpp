#include "SceneSerializer.h"
#include <QStringList>

namespace Raytracer {
namespace scene {

SceneSerializerFactory& SceneSerializerFactory::instance()
{
    static SceneSerializerFactory factory;
    return factory;
}

void SceneSerializerFactory::registerSerializer(std::unique_ptr<ISceneSerializer> serializer)
{
    if (serializer) {
        m_serializers.push_back(std::move(serializer));
    }
}

ISceneSerializer* SceneSerializerFactory::getSerializer(const QString &filePath) const
{
    for (const auto &serializer : m_serializers) {
        if (serializer->canHandle(filePath)) {
            return serializer.get();
        }
    }
    return nullptr;
}

ISceneSerializer* SceneSerializerFactory::getSerializerByExtension(const QString &extension) const
{
    QString ext = extension.toLower();
    if (ext.startsWith('.')) {
        ext = ext.mid(1);
    }
    
    for (const auto &serializer : m_serializers) {
        if (serializer->fileExtension().toLower() == ext) {
            return serializer.get();
        }
    }
    return nullptr;
}

QString SceneSerializerFactory::getAllFilters() const
{
    QStringList filters;
    for (const auto &serializer : m_serializers) {
        filters << serializer->fileFilter();
    }
    
    if (filters.isEmpty()) {
        return QStringLiteral("All Files (*)");
    }
    
    // Add "All supported formats" filter at the beginning
    QStringList allExtensions;
    for (const auto &serializer : m_serializers) {
        allExtensions << QStringLiteral("*.%1").arg(serializer->fileExtension());
    }
    
    QString allFilter = QStringLiteral("All Scene Files (%1)").arg(allExtensions.join(' '));
    filters.prepend(allFilter);
    
    return filters.join(QStringLiteral(";;"));
}

QStringList SceneSerializerFactory::supportedExtensions() const
{
    QStringList extensions;
    for (const auto &serializer : m_serializers) {
        extensions << serializer->fileExtension();
    }
    return extensions;
}

} // namespace scene
} // namespace Raytracer
