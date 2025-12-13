#ifndef RAYTRACER_XML_SCENE_SERIALIZER_H
#define RAYTRACER_XML_SCENE_SERIALIZER_H

#include "SceneSerializer.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"
#include "../models/MaterialDefinition.h"
#include "../../3rdParty/tinyxml2/tinyxml2.h"

namespace Raytracer {
namespace scene {

/**
 * @brief XML serializer for scene documents
 * 
 * This serializer uses a format compatible with the existing raytracer
 * XML scene files while adding support for the scene editor's features
 * like hierarchical nodes and material libraries.
 */
class XMLSceneSerializer : public ISceneSerializer {
public:
    XMLSceneSerializer() = default;
    ~XMLSceneSerializer() override = default;
    
    QString fileExtension() const override { return QStringLiteral("xml"); }
    QString formatName() const override { return QStringLiteral("XML Scene Files"); }
    QString fileFilter() const override { return QStringLiteral("XML Scene Files (*.xml)"); }
    
    SerializationResult load(const QString &filePath, SceneDocument *document) override;
    SerializationResult save(const QString &filePath, const SceneDocument *document) override;
    SerializationResult loadFromString(const QString &content, SceneDocument *document) override;
    SerializationResult saveToString(const SceneDocument *document, QString &output) override;
    
private:
    // Loading helpers
    SerializationResult loadFromXMLDocument(tinyxml2::XMLDocument &doc, SceneDocument *document);
    bool loadConfig(tinyxml2::XMLElement *configElem, SceneDocument *document);
    bool loadCamera(tinyxml2::XMLElement *cameraElem, SceneDocument *document);
    bool loadLights(tinyxml2::XMLElement *lightsElem, SceneDocument *document);
    bool loadMaterials(tinyxml2::XMLElement *materialsElem, SceneDocument *document);
    bool loadObjects(tinyxml2::XMLElement *objectsElem, SceneDocument *document);
    bool loadSphere(tinyxml2::XMLElement *elem, SceneDocument *document, SceneNode *parent);
    bool loadTriangle(tinyxml2::XMLElement *elem, SceneDocument *document, SceneNode *parent);
    bool loadMesh(tinyxml2::XMLElement *elem, SceneDocument *document, SceneNode *parent);
    bool loadGroup(tinyxml2::XMLElement *elem, SceneDocument *document, SceneNode *parent);
    
    // Saving helpers
    void saveConfig(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *root, 
                   const SceneDocument *document);
    void saveCamera(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *root, 
                   const SceneDocument *document);
    void saveLights(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *root, 
                   const SceneDocument *document);
    void saveMaterials(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *root, 
                      const SceneDocument *document);
    void saveObjects(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *root, 
                    const SceneDocument *document);
    void saveNode(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, 
                 const SceneNode *node, const SceneDocument *document);
    
    // Utility helpers
    glm::vec3 readVec3(tinyxml2::XMLElement *elem, const char *xAttr = "x", 
                       const char *yAttr = "y", const char *zAttr = "z");
    void writeVec3(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, 
                  const char *name, const glm::vec3 &v, 
                  const char *xAttr = "x", const char *yAttr = "y", const char *zAttr = "z");
    void writeVec3Rgb(tinyxml2::XMLDocument &doc, tinyxml2::XMLElement *parent, 
                     const char *name, const glm::vec3 &v);
    glm::vec3 readColorRgb(tinyxml2::XMLElement *elem);
    
    QString materialNameFromId(const QUuid &id, const SceneDocument *document);
    QUuid materialIdFromName(const QString &name, const SceneDocument *document);
};

/**
 * @brief Register the XML serializer with the factory
 * Call this during application startup
 */
void registerXMLSceneSerializer();

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_XML_SCENE_SERIALIZER_H
