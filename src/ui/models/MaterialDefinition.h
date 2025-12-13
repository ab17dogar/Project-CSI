#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QUuid>
#include <glm/glm.hpp>
#include <memory>

namespace Raytracer {
namespace scene {

/**
 * @brief Material types supported by the raytracer
 */
enum class MaterialType {
  Lambertian, // Diffuse material
  Metal,      // Reflective metal with optional fuzz
  Emissive,   // Light-emitting material
  Dielectric, // Glass/transparent (future)
  PBR         // Physically-based rendering (future)
};

/**
 * @brief Material definition for scene editing.
 *
 * This is the editable representation of a material, separate from
 * the render-time material classes. Allows non-destructive editing
 * and serialization.
 */
class MaterialDefinition : public QObject {
  Q_OBJECT

public:
  explicit MaterialDefinition(QObject *parent = nullptr);
  MaterialDefinition(const QString &name, MaterialType type,
                     QObject *parent = nullptr);

  // Unique identifier
  QUuid uuid() const { return m_uuid; }

  // Display name
  QString name() const { return m_name; }
  void setName(const QString &name);

  // Material type
  MaterialType type() const { return m_type; }
  void setType(MaterialType type);

  // Base color (albedo for Lambertian, tint for Metal)
  glm::vec3 color() const { return m_color; }
  QColor qColor() const;
  void setColor(const glm::vec3 &color);
  void setColor(float r, float g, float b) { setColor(glm::vec3(r, g, b)); }
  void setQColor(const QColor &color);

  // Metal properties
  float fuzz() const { return m_fuzz; }
  void setFuzz(float fuzz);

  // Emissive properties
  float emissiveStrength() const { return m_emissiveStrength; }
  void setEmissiveStrength(float strength);

  // Dielectric properties (future)
  float refractiveIndex() const { return m_refractiveIndex; }
  void setRefractiveIndex(float ior);

  // Roughness (for PBR, future)
  float roughness() const { return m_roughness; }
  void setRoughness(float roughness);

  // Metallic (for PBR, future)
  float metallic() const { return m_metallic; }
  void setMetallic(float metallic);

  // Clone this material with a new UUID
  std::unique_ptr<MaterialDefinition> clone() const;

  // Create default materials
  static std::unique_ptr<MaterialDefinition> createDefault();
  static std::unique_ptr<MaterialDefinition>
  createLambertian(const QString &name, const glm::vec3 &color);
  static std::unique_ptr<MaterialDefinition>
  createMetal(const QString &name, const glm::vec3 &color, float fuzz);
  static std::unique_ptr<MaterialDefinition>
  createEmissive(const QString &name, const glm::vec3 &color, float strength);
  static std::unique_ptr<MaterialDefinition>
  createDielectric(const QString &name, float ior = 1.5f);

  // String conversion for serialization
  static QString typeToString(MaterialType type);
  static MaterialType stringToType(const QString &str);

signals:
  void changed();
  void nameChanged(const QString &name);
  void typeChanged(MaterialType type);
  void colorChanged(const glm::vec3 &color);

private:
  QUuid m_uuid;
  QString m_name{"Default"};
  MaterialType m_type{MaterialType::Lambertian};

  // Common properties
  glm::vec3 m_color{0.8f, 0.8f, 0.8f};

  // Metal-specific
  float m_fuzz{0.0f};

  // Emissive-specific
  float m_emissiveStrength{1.0f};

  // Dielectric-specific
  float m_refractiveIndex{1.5f};

  // PBR properties (future)
  float m_roughness{0.5f};
  float m_metallic{0.0f};
};

} // namespace scene
} // namespace Raytracer
