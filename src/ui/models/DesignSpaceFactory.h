#pragma once

#include <QString>

namespace Raytracer {
namespace scene {

class SceneDocument;

/**
 * @brief Configuration for room-type design spaces
 */
struct RoomConfig {
  float width = 10.0f;
  float depth = 10.0f;
  float height = 6.0f;
  bool hasCeiling = true;
  bool hasFloorLight = false;
  bool hasCeilingLight = true;
  QString floorMaterial = "floor_wood";
  QString wallMaterial = "wall_white";
  QString accentWallMaterial = "wall_accent";
};

/**
 * @brief Factory for creating reusable design spaces
 *
 * This factory provides preset design environments for scene editing.
 * Users can select from presets or customize room parameters.
 *
 * Designed for extensibility - add new presets by:
 * 1. Adding to PresetType enum
 * 2. Adding case in applyPreset()
 */
class DesignSpaceFactory {
public:
  /**
   * @brief Available preset design space types
   */
  enum class PresetType {
    Empty,        // No design space, just grid
    IndoorRoom,   // Standard room with walls, floor, ceiling
    StudioSetup,  // Photography studio with lighting
    OutdoorScene, // Ground plane with sky
    // Future presets:
    // Gallery,
    // Warehouse,
    // CustomRoom
  };

  /**
   * @brief Apply a preset design space to the document
   * @param document Target document to modify
   * @param preset Type of preset to apply
   */
  static void applyPreset(SceneDocument *document, PresetType preset);

  /**
   * @brief Create a custom room with specified dimensions
   * @param document Target document to modify
   * @param config Room configuration
   */
  static void createRoom(SceneDocument *document, const RoomConfig &config);

  /**
   * @brief Get display name for a preset type
   * @param preset Preset to get name for
   * @return Human-readable name
   */
  static QString presetName(PresetType preset);

  /**
   * @brief Get list of all available presets
   * @return List of preset types
   */
  static QList<PresetType> availablePresets();

private:
  static void setupRoomMaterials(SceneDocument *document);
  static void createFloor(SceneDocument *document, const RoomConfig &config);
  static void createWalls(SceneDocument *document, const RoomConfig &config);
  static void createCeiling(SceneDocument *document, const RoomConfig &config);
  static void createLighting(SceneDocument *document, const RoomConfig &config);
  static void setupCamera(SceneDocument *document, const RoomConfig &config);
};

} // namespace scene
} // namespace Raytracer
