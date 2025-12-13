#ifndef RAYTRACER_SCENE_CONVERTER_H
#define RAYTRACER_SCENE_CONVERTER_H

#include <memory>
#include <string>
#include <QHash>
#include <QUuid>

// Forward declarations
class world;
class hittable;
class material;
class config;
class camera;
class sun;

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneNode;
class MaterialDefinition;

/**
 * @brief Converts SceneDocument to render World
 * 
 * This service bridges the scene editor's data model with the
 * raytracer's rendering engine. It creates all necessary
 * render objects from the editor's scene representation.
 */
class SceneConverter {
public:
    SceneConverter() = default;
    ~SceneConverter() = default;
    
    /**
     * @brief Convert a scene document to a render world
     * @param document Source scene document
     * @return Shared pointer to the created world, or nullptr on error
     */
    std::shared_ptr<world> convertToWorld(const SceneDocument *document);
    
    /**
     * @brief Get the last error message
     * @return Error description or empty string if no error
     */
    std::string lastError() const { return m_lastError; }
    
private:
    // Conversion helpers
    std::shared_ptr<config> convertConfig(const SceneDocument *document);
    std::shared_ptr<camera> convertCamera(const SceneDocument *document, float aspectRatio);
    std::shared_ptr<sun> convertSun(const SceneDocument *document);
    
    // Material conversion
    std::shared_ptr<material> convertMaterial(const MaterialDefinition *matDef);
    std::shared_ptr<material> getMaterial(const QUuid &materialId, const SceneDocument *document);
    
    // Object conversion
    std::shared_ptr<hittable> convertNode(const SceneNode *node, const SceneDocument *document);
    std::shared_ptr<hittable> convertSphere(const SceneNode *node, const SceneDocument *document);
    std::shared_ptr<hittable> convertTriangle(const SceneNode *node, const SceneDocument *document);
    std::shared_ptr<hittable> convertMesh(const SceneNode *node, const SceneDocument *document);
    
    // Utility
    void setError(const std::string &error) { m_lastError = error; }
    void clearError() { m_lastError.clear(); }
    
private:
    std::string m_lastError;
    
    // Material cache for the current conversion
    QHash<QUuid, std::shared_ptr<material>> m_materialCache;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_SCENE_CONVERTER_H
