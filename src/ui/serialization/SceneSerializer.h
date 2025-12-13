#ifndef RAYTRACER_SCENE_SERIALIZER_H
#define RAYTRACER_SCENE_SERIALIZER_H

#include <QString>
#include <memory>

namespace Raytracer {
namespace scene {

class SceneDocument;

/**
 * @brief Result of a serialization operation
 */
struct SerializationResult {
    bool success = false;
    QString errorMessage;
    int errorLine = -1;
    int errorColumn = -1;
    
    static SerializationResult ok() {
        return SerializationResult{true, QString(), -1, -1};
    }
    
    static SerializationResult error(const QString &message, int line = -1, int column = -1) {
        return SerializationResult{false, message, line, column};
    }
};

/**
 * @brief Abstract interface for scene serialization
 * 
 * Defines the contract for loading and saving scene documents.
 * Implementations can support different file formats (XML, JSON, binary, etc.)
 */
class ISceneSerializer {
public:
    virtual ~ISceneSerializer() = default;
    
    /**
     * @brief Get the file extension this serializer handles
     * @return File extension without dot (e.g., "xml", "json")
     */
    virtual QString fileExtension() const = 0;
    
    /**
     * @brief Get a display name for the file type
     * @return Human-readable format name (e.g., "XML Scene Files")
     */
    virtual QString formatName() const = 0;
    
    /**
     * @brief Get file dialog filter string
     * @return Filter string (e.g., "XML Scene Files (*.xml)")
     */
    virtual QString fileFilter() const = 0;
    
    /**
     * @brief Load a scene document from a file
     * @param filePath Path to the file to load
     * @param document Target document to populate
     * @return Result indicating success or failure with error details
     */
    virtual SerializationResult load(const QString &filePath, SceneDocument *document) = 0;
    
    /**
     * @brief Save a scene document to a file
     * @param filePath Path to save to
     * @param document Source document to serialize
     * @return Result indicating success or failure with error details
     */
    virtual SerializationResult save(const QString &filePath, const SceneDocument *document) = 0;
    
    /**
     * @brief Load a scene document from a string
     * @param content String content to parse
     * @param document Target document to populate
     * @return Result indicating success or failure
     */
    virtual SerializationResult loadFromString(const QString &content, SceneDocument *document) = 0;
    
    /**
     * @brief Save a scene document to a string
     * @param document Source document to serialize
     * @param output String to write to
     * @return Result indicating success or failure
     */
    virtual SerializationResult saveToString(const SceneDocument *document, QString &output) = 0;
    
    /**
     * @brief Check if this serializer can handle a given file
     * @param filePath Path to check
     * @return true if this serializer can handle the file
     */
    virtual bool canHandle(const QString &filePath) const {
        return filePath.endsWith("." + fileExtension(), Qt::CaseInsensitive);
    }
};

/**
 * @brief Factory for creating scene serializers
 * 
 * Allows registration and retrieval of serializers by format
 */
class SceneSerializerFactory {
public:
    static SceneSerializerFactory& instance();
    
    /**
     * @brief Register a serializer for a file format
     * @param serializer Serializer instance (factory takes ownership)
     */
    void registerSerializer(std::unique_ptr<ISceneSerializer> serializer);
    
    /**
     * @brief Get a serializer that can handle a file
     * @param filePath File to find serializer for
     * @return Pointer to serializer, or nullptr if none found
     */
    ISceneSerializer* getSerializer(const QString &filePath) const;
    
    /**
     * @brief Get a serializer by format extension
     * @param extension File extension (without dot)
     * @return Pointer to serializer, or nullptr if none found
     */
    ISceneSerializer* getSerializerByExtension(const QString &extension) const;
    
    /**
     * @brief Get combined file filter for all registered formats
     * @return Filter string for file dialogs
     */
    QString getAllFilters() const;
    
    /**
     * @brief Get list of all supported extensions
     * @return List of file extensions
     */
    QStringList supportedExtensions() const;
    
private:
    SceneSerializerFactory() = default;
    std::vector<std::unique_ptr<ISceneSerializer>> m_serializers;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SCENE_SERIALIZER_H
