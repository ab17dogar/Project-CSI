#include "DesignSpaceFactory.h"
#include "MaterialDefinition.h"
#include "SceneDocument.h"
#include "SceneNode.h"
#include <glm/glm.hpp>

namespace Raytracer {
namespace scene {

void DesignSpaceFactory::applyPreset(SceneDocument *document,
                                     PresetType preset) {
  if (!document)
    return;

  document->clear();
  setupRoomMaterials(document);

  switch (preset) {
  case PresetType::Empty:
    // Just materials, no geometry
    break;

  case PresetType::IndoorRoom: {
    RoomConfig config;
    config.width = 10.0f;
    config.depth = 10.0f;
    config.height = 6.0f;
    config.hasCeiling = true;
    config.hasCeilingLight = true;
    createRoom(document, config);
    break;
  }

  case PresetType::StudioSetup: {
    RoomConfig config;
    config.width = 15.0f;
    config.depth = 12.0f;
    config.height = 8.0f;
    config.hasCeiling = false; // Open ceiling for studio feel
    config.hasCeilingLight = true;
    createRoom(document, config);
    break;
  }

  case PresetType::OutdoorScene: {
    // Just a ground plane
    RoomConfig config;
    config.width = 20.0f;
    config.depth = 20.0f;
    config.height = 0.0f; // No walls
    config.hasCeiling = false;
    config.hasCeilingLight = false;
    createFloor(document, config);
    setupCamera(document, config);
    break;
  }
  }

  document->setDirty(false);
  emit document->documentChanged();
}

void DesignSpaceFactory::createRoom(SceneDocument *document,
                                    const RoomConfig &config) {
  createFloor(document, config);
  if (config.height > 0) {
    createWalls(document, config);
  }
  if (config.hasCeiling) {
    createCeiling(document, config);
  }
  createLighting(document, config);
  setupCamera(document, config);
}

QString DesignSpaceFactory::presetName(PresetType preset) {
  switch (preset) {
  case PresetType::Empty:
    return "Empty Scene";
  case PresetType::IndoorRoom:
    return "Indoor Room";
  case PresetType::StudioSetup:
    return "Studio Setup";
  case PresetType::OutdoorScene:
    return "Outdoor Scene";
  }
  return "Unknown";
}

QList<DesignSpaceFactory::PresetType> DesignSpaceFactory::availablePresets() {
  return {PresetType::Empty, PresetType::IndoorRoom, PresetType::StudioSetup,
          PresetType::OutdoorScene};
}

void DesignSpaceFactory::setupRoomMaterials(SceneDocument *document) {
  // Room-specific materials
  document->addMaterial(MaterialDefinition::createLambertian(
      "floor_wood", glm::vec3(0.55f, 0.35f, 0.2f)));
  document->addMaterial(MaterialDefinition::createLambertian(
      "wall_white", glm::vec3(0.92f, 0.9f, 0.88f)));
  document->addMaterial(MaterialDefinition::createLambertian(
      "wall_accent", glm::vec3(0.4f, 0.5f, 0.6f)));

  // Standard object materials
  document->addMaterial(MaterialDefinition::createLambertian(
      "ground", glm::vec3(0.8f, 0.8f, 0.0f)));
  document->addMaterial(MaterialDefinition::createLambertian(
      "mattBrown", glm::vec3(0.7f, 0.3f, 0.3f)));
  document->addMaterial(MaterialDefinition::createMetal(
      "fuzzySilver", glm::vec3(0.8f, 0.8f, 0.8f), 0.3f));
  document->addMaterial(MaterialDefinition::createMetal(
      "shinyGold", glm::vec3(0.8f, 0.6f, 0.2f), 0.0f));
  document->addMaterial(MaterialDefinition::createEmissive(
      "emissive", glm::vec3(1.0f, 1.0f, 1.0f), 4.0f)); // Brighter light

  // Glass material (Dielectric)
  document->addMaterial(MaterialDefinition::createDielectric("glass", 1.5f));
  document->addMaterial(MaterialDefinition::createDielectric("water", 1.33f));
  document->addMaterial(MaterialDefinition::createDielectric("diamond", 2.4f));
}

void DesignSpaceFactory::createFloor(SceneDocument *document,
                                     const RoomConfig &config) {
  const float hw = config.width / 2.0f;
  const float hd = config.depth / 2.0f;

  auto *floorMat = document->findMaterialByName(config.floorMaterial);
  QUuid matId = floorMat ? floorMat->uuid() : QUuid();

  auto floor1 = SceneNode::createTriangle("Floor1");
  floor1->geometryParams().v0 = glm::vec3(-hw, 0.0f, -hd);
  floor1->geometryParams().v1 = glm::vec3(hw, 0.0f, -hd);
  floor1->geometryParams().v2 = glm::vec3(-hw, 0.0f, hd);
  floor1->setMaterialId(matId);
  document->addNode(std::move(floor1));

  auto floor2 = SceneNode::createTriangle("Floor2");
  floor2->geometryParams().v0 = glm::vec3(hw, 0.0f, -hd);
  floor2->geometryParams().v1 = glm::vec3(hw, 0.0f, hd);
  floor2->geometryParams().v2 = glm::vec3(-hw, 0.0f, hd);
  floor2->setMaterialId(matId);
  document->addNode(std::move(floor2));
}

void DesignSpaceFactory::createWalls(SceneDocument *document,
                                     const RoomConfig &config) {
  const float hw = config.width / 2.0f;
  const float hd = config.depth / 2.0f;
  const float h = config.height;

  auto *wallMat = document->findMaterialByName(config.wallMaterial);
  auto *accentMat = document->findMaterialByName(config.accentWallMaterial);
  QUuid wallId = wallMat ? wallMat->uuid() : QUuid();
  QUuid accentId = accentMat ? accentMat->uuid() : QUuid();

  // Back wall (Z = -hd)
  auto bw1 = SceneNode::createTriangle("BackWall1");
  bw1->geometryParams().v0 = glm::vec3(-hw, 0.0f, -hd);
  bw1->geometryParams().v1 = glm::vec3(-hw, h, -hd);
  bw1->geometryParams().v2 = glm::vec3(hw, 0.0f, -hd);
  bw1->setMaterialId(wallId);
  document->addNode(std::move(bw1));

  auto bw2 = SceneNode::createTriangle("BackWall2");
  bw2->geometryParams().v0 = glm::vec3(-hw, h, -hd);
  bw2->geometryParams().v1 = glm::vec3(hw, h, -hd);
  bw2->geometryParams().v2 = glm::vec3(hw, 0.0f, -hd);
  bw2->setMaterialId(wallId);
  document->addNode(std::move(bw2));

  // Left wall (X = -hw) - accent color
  auto lw1 = SceneNode::createTriangle("LeftWall1");
  lw1->geometryParams().v0 = glm::vec3(-hw, 0.0f, hd);
  lw1->geometryParams().v1 = glm::vec3(-hw, h, hd);
  lw1->geometryParams().v2 = glm::vec3(-hw, 0.0f, -hd);
  lw1->setMaterialId(accentId);
  document->addNode(std::move(lw1));

  auto lw2 = SceneNode::createTriangle("LeftWall2");
  lw2->geometryParams().v0 = glm::vec3(-hw, h, hd);
  lw2->geometryParams().v1 = glm::vec3(-hw, h, -hd);
  lw2->geometryParams().v2 = glm::vec3(-hw, 0.0f, -hd);
  lw2->setMaterialId(accentId);
  document->addNode(std::move(lw2));

  // Right wall (X = hw)
  auto rw1 = SceneNode::createTriangle("RightWall1");
  rw1->geometryParams().v0 = glm::vec3(hw, 0.0f, -hd);
  rw1->geometryParams().v1 = glm::vec3(hw, h, -hd);
  rw1->geometryParams().v2 = glm::vec3(hw, 0.0f, hd);
  rw1->setMaterialId(wallId);
  document->addNode(std::move(rw1));

  auto rw2 = SceneNode::createTriangle("RightWall2");
  rw2->geometryParams().v0 = glm::vec3(hw, h, -hd);
  rw2->geometryParams().v1 = glm::vec3(hw, h, hd);
  rw2->geometryParams().v2 = glm::vec3(hw, 0.0f, hd);
  rw2->setMaterialId(wallId);
  document->addNode(std::move(rw2));
}

void DesignSpaceFactory::createCeiling(SceneDocument *document,
                                       const RoomConfig &config) {
  const float hw = config.width / 2.0f;
  const float hd = config.depth / 2.0f;
  const float h = config.height;

  auto *wallMat = document->findMaterialByName(config.wallMaterial);
  QUuid matId = wallMat ? wallMat->uuid() : QUuid();

  auto c1 = SceneNode::createTriangle("Ceiling1");
  c1->geometryParams().v0 = glm::vec3(-hw, h, hd);
  c1->geometryParams().v1 = glm::vec3(hw, h, hd);
  c1->geometryParams().v2 = glm::vec3(-hw, h, -hd);
  c1->setMaterialId(matId);
  document->addNode(std::move(c1));

  auto c2 = SceneNode::createTriangle("Ceiling2");
  c2->geometryParams().v0 = glm::vec3(hw, h, hd);
  c2->geometryParams().v1 = glm::vec3(hw, h, -hd);
  c2->geometryParams().v2 = glm::vec3(-hw, h, -hd);
  c2->setMaterialId(matId);
  document->addNode(std::move(c2));
}

void DesignSpaceFactory::createLighting(SceneDocument *document,
                                        const RoomConfig &config) {
  if (config.hasCeilingLight && config.height > 0) {
    auto *emissive = document->findMaterialByName("emissive");
    if (emissive) {
      auto light = SceneNode::createSphere("Ceiling Light", 0.8f);
      light->transform()->setPosition(0.0f, config.height - 0.5f, 0.0f);
      light->setMaterialId(emissive->uuid());
      document->addNode(std::move(light));
    }
  }

  // Add sample gold object
  auto *gold = document->findMaterialByName("shinyGold");
  if (gold) {
    auto sample = SceneNode::createSphere("Gold Sphere", 0.5f);
    sample->transform()->setPosition(-1.0f, 0.5f, 0.0f);
    sample->setMaterialId(gold->uuid());
    document->addNode(std::move(sample));
  }

  // Add glass sphere to demonstrate refraction
  auto *glass = document->findMaterialByName("glass");
  if (glass) {
    auto glassSphere = SceneNode::createSphere("Glass Sphere", 0.6f);
    glassSphere->transform()->setPosition(1.0f, 0.6f, 0.0f);
    glassSphere->setMaterialId(glass->uuid());
    document->addNode(std::move(glassSphere));
  }

  // Interior sun settings
  document->sun().direction = glm::vec3(0.2f, 0.8f, 0.3f);
  document->sun().intensity = 0.6f;
  document->sun().color = glm::vec3(1.0f, 0.95f, 0.9f);
}

void DesignSpaceFactory::setupCamera(SceneDocument *document,
                                     const RoomConfig &config) {
  // Position camera to see the room from a good angle
  float viewDist = std::max(config.width, config.depth) * 0.8f;

  document->camera().lookFrom =
      glm::vec3(viewDist * 0.6f, config.height * 0.4f, viewDist * 0.8f);
  document->camera().lookAt = glm::vec3(0.0f, config.height * 0.25f, 0.0f);
  document->camera().fov = 60.0f;
}

} // namespace scene
} // namespace Raytracer
