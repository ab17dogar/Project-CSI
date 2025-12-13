#include "SceneConverter.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"
#include "../models/MaterialDefinition.h"

#include "../../engine/world.h"
#include "../../engine/config.h"
#include "../../engine/camera.h"
#include "../../engine/sun.h"
#include "../../engine/sphere.h"
#include "../../engine/triangle.h"
#include "../../engine/mesh.h"
#include "../../engine/lambertian.h"
#include "../../engine/metal.h"
#include "../../engine/emissive.h"

#include <QUuid>
#include <glm/glm.hpp>

namespace Raytracer {
namespace scene {

std::shared_ptr<world> SceneConverter::convertToWorld(const SceneDocument *document)
{
    clearError();
    m_materialCache.clear();
    
    if (!document) {
        setError("Null document");
        return nullptr;
    }
    
    auto pworld = std::make_shared<world>();
    
    // Convert config
    pworld->pconfig = convertConfig(document);
    if (!pworld->pconfig) {
        setError("Failed to convert config");
        return nullptr;
    }
    
    // Convert camera
    pworld->pcamera = convertCamera(document, pworld->pconfig->ASPECT_RATIO);
    if (!pworld->pcamera) {
        setError("Failed to convert camera");
        return nullptr;
    }
    
    // Convert sun
    pworld->psun = convertSun(document);
    if (!pworld->psun) {
        setError("Failed to convert sun");
        return nullptr;
    }
    
    // Convert materials first and cache them
    for (const auto &matDef : document->materials()) {
        auto mat = convertMaterial(matDef.get());
        if (mat) {
            m_materialCache.insert(matDef->uuid(), mat);
            pworld->materials.push_back(mat);
        }
    }
    
    // Convert scene objects
    const SceneNode *root = document->rootNode();
    if (root) {
        for (SceneNode *child : root->children()) {
            auto obj = convertNode(child, document);
            if (obj) {
                pworld->objects.push_back(obj);
            }
        }
    }
    
    // Build BVH if using acceleration
    if (pworld->GetAccelerationMethod() == AccelerationMethod::BVH) {
        pworld->buildBVH();
    }
    
    return pworld;
}

std::shared_ptr<config> SceneConverter::convertConfig(const SceneDocument *document)
{
    const RenderConfig &cfg = document->renderConfig();
    
    auto pconfig = std::make_shared<config>();
    pconfig->IMAGE_WIDTH = cfg.width;
    pconfig->IMAGE_HEIGHT = cfg.height;
    pconfig->ASPECT_RATIO = static_cast<float>(cfg.width) / static_cast<float>(cfg.height);
    pconfig->SAMPLES_PER_PIXEL = cfg.samplesPerPixel;
    pconfig->MAX_DEPTH = cfg.maxDepth;
    
    return pconfig;
}

std::shared_ptr<camera> SceneConverter::convertCamera(const SceneDocument *document, float aspectRatio)
{
    const CameraSettings &cam = document->camera();
    
    vec3 lookFrom(cam.lookFrom.x, cam.lookFrom.y, cam.lookFrom.z);
    vec3 lookAt(cam.lookAt.x, cam.lookAt.y, cam.lookAt.z);
    vec3 up(cam.up.x, cam.up.y, cam.up.z);
    
    return std::make_shared<camera>(lookFrom, lookAt, up, cam.fov, aspectRatio);
}

std::shared_ptr<sun> SceneConverter::convertSun(const SceneDocument *document)
{
    const SunSettings &sunSettings = document->sun();
    
    vec3 direction(sunSettings.direction.x, sunSettings.direction.y, sunSettings.direction.z);
    color sunColor(sunSettings.color.x, sunSettings.color.y, sunSettings.color.z);
    
    // Scale by intensity
    sunColor = sunColor * sunSettings.intensity;
    
    return std::make_shared<sun>(direction, sunColor);
}

std::shared_ptr<material> SceneConverter::convertMaterial(const MaterialDefinition *matDef)
{
    if (!matDef) return nullptr;
    
    const glm::vec3 &c = matDef->color();
    color matColor(c.r, c.g, c.b);
    
    switch (matDef->type()) {
        case MaterialType::Lambertian:
            return std::make_shared<lambertian>(matColor);
            
        case MaterialType::Metal:
            return std::make_shared<metal>(matColor, matDef->fuzz());
            
        case MaterialType::Emissive:
            // Emissive scales color by strength
            return std::make_shared<emissive>(matColor * matDef->emissiveStrength());
            
        case MaterialType::Dielectric:
            // Dielectric not implemented - use lambertian fallback
            return std::make_shared<lambertian>(matColor);
            
        default:
            return std::make_shared<lambertian>(color(0.5, 0.5, 0.5));
    }
}

std::shared_ptr<material> SceneConverter::getMaterial(const QUuid &materialId, const SceneDocument *document)
{
    if (materialId.isNull()) {
        // Return default material
        return std::make_shared<lambertian>(color(0.7, 0.7, 0.7));
    }
    
    auto it = m_materialCache.constFind(materialId);
    if (it != m_materialCache.constEnd()) {
        return it.value();
    }
    
    // Not in cache - try to convert
    if (auto *matDef = document->findMaterial(materialId)) {
        auto mat = convertMaterial(matDef);
        if (mat) {
            m_materialCache.insert(materialId, mat);
            return mat;
        }
    }
    
    // Return default material
    return std::make_shared<lambertian>(color(0.7, 0.7, 0.7));
}

std::shared_ptr<hittable> SceneConverter::convertNode(const SceneNode *node, const SceneDocument *document)
{
    if (!node || !node->isVisible()) return nullptr;
    
    std::shared_ptr<hittable> obj;
    
    switch (node->geometryType()) {
        case GeometryType::Sphere:
            obj = convertSphere(node, document);
            break;
            
        case GeometryType::Triangle:
            obj = convertTriangle(node, document);
            break;
            
        case GeometryType::Mesh:
            obj = convertMesh(node, document);
            break;
            
        case GeometryType::None:
        case GeometryType::Plane:
        case GeometryType::Cube:
        default:
            return nullptr;
    }
    
    return obj;
}

std::shared_ptr<hittable> SceneConverter::convertSphere(const SceneNode *node, const SceneDocument *document)
{
    if (!node) return nullptr;
    
    const glm::vec3 &pos = node->transform()->position();
    float radius = node->geometryParams().radius;
    
    auto mat = getMaterial(node->materialId(), document);
    
    vec3 center(pos.x, pos.y, pos.z);
    return std::make_shared<sphere>(center, radius, mat);
}

std::shared_ptr<hittable> SceneConverter::convertTriangle(const SceneNode *node, const SceneDocument *document)
{
    if (!node) return nullptr;
    
    const GeometryParams &params = node->geometryParams();
    
    vec3 v0(params.v0.x, params.v0.y, params.v0.z);
    vec3 v1(params.v1.x, params.v1.y, params.v1.z);
    vec3 v2(params.v2.x, params.v2.y, params.v2.z);
    
    auto mat = getMaterial(node->materialId(), document);
    
    return std::make_shared<triangle>(v0, v1, v2, mat);
}

std::shared_ptr<hittable> SceneConverter::convertMesh(const SceneNode *node, const SceneDocument *document)
{
    if (!node) return nullptr;
    
    const glm::vec3 &pos = node->transform()->position();
    const glm::vec3 &rot = node->transform()->rotation();
    const glm::vec3 &scl = node->transform()->scale();
    
    vec3 position(pos.x, pos.y, pos.z);
    vec3 rotation(rot.x, rot.y, rot.z);
    vec3 scaleVec(scl.x, scl.y, scl.z);
    
    auto mat = getMaterial(node->materialId(), document);
    QString meshFilePath = node->geometryParams().meshFilePath;
    
    // mesh constructor: mesh(file, position, scale, rotation, material)
    auto meshObj = std::make_shared<mesh>(
        meshFilePath.toStdString(),
        position,
        scaleVec,
        rotation,
        mat
    );
    
    if (meshObj->getTriangleCount() == 0) {
        // Mesh failed to load
        setError("Failed to load mesh: " + meshFilePath.toStdString());
        return nullptr;
    }
    
    return meshObj;
}

} // namespace scene
} // namespace Raytracer
