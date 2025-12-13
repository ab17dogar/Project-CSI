#include "MaterialDefinition.h"
#include <algorithm>

namespace Raytracer {
namespace scene {

MaterialDefinition::MaterialDefinition(QObject *parent)
    : QObject(parent), m_uuid(QUuid::createUuid()) {}

MaterialDefinition::MaterialDefinition(const QString &name, MaterialType type,
                                       QObject *parent)
    : QObject(parent), m_uuid(QUuid::createUuid()), m_name(name), m_type(type) {
}

void MaterialDefinition::setName(const QString &name) {
  if (m_name != name) {
    m_name = name;
    emit nameChanged(name);
    emit changed();
  }
}

void MaterialDefinition::setType(MaterialType type) {
  if (m_type != type) {
    m_type = type;
    emit typeChanged(type);
    emit changed();
  }
}

QColor MaterialDefinition::qColor() const {
  return QColor::fromRgbF(std::clamp(m_color.r, 0.0f, 1.0f),
                          std::clamp(m_color.g, 0.0f, 1.0f),
                          std::clamp(m_color.b, 0.0f, 1.0f));
}

void MaterialDefinition::setColor(const glm::vec3 &color) {
  if (m_color != color) {
    m_color = color;
    emit colorChanged(color);
    emit changed();
  }
}

void MaterialDefinition::setQColor(const QColor &color) {
  setColor(glm::vec3(color.redF(), color.greenF(), color.blueF()));
}

void MaterialDefinition::setFuzz(float fuzz) {
  fuzz = std::clamp(fuzz, 0.0f, 1.0f);
  if (m_fuzz != fuzz) {
    m_fuzz = fuzz;
    emit changed();
  }
}

void MaterialDefinition::setEmissiveStrength(float strength) {
  strength = std::max(0.0f, strength);
  if (m_emissiveStrength != strength) {
    m_emissiveStrength = strength;
    emit changed();
  }
}

void MaterialDefinition::setRefractiveIndex(float ior) {
  ior = std::max(1.0f, ior);
  if (m_refractiveIndex != ior) {
    m_refractiveIndex = ior;
    emit changed();
  }
}

void MaterialDefinition::setRoughness(float roughness) {
  roughness = std::clamp(roughness, 0.0f, 1.0f);
  if (m_roughness != roughness) {
    m_roughness = roughness;
    emit changed();
  }
}

void MaterialDefinition::setMetallic(float metallic) {
  metallic = std::clamp(metallic, 0.0f, 1.0f);
  if (m_metallic != metallic) {
    m_metallic = metallic;
    emit changed();
  }
}

std::unique_ptr<MaterialDefinition> MaterialDefinition::clone() const {
  auto copy = std::make_unique<MaterialDefinition>(m_name + " (Copy)", m_type);
  copy->m_color = m_color;
  copy->m_fuzz = m_fuzz;
  copy->m_emissiveStrength = m_emissiveStrength;
  copy->m_refractiveIndex = m_refractiveIndex;
  copy->m_roughness = m_roughness;
  copy->m_metallic = m_metallic;
  return copy;
}

std::unique_ptr<MaterialDefinition> MaterialDefinition::createDefault() {
  return createLambertian("Default", glm::vec3(0.8f, 0.8f, 0.8f));
}

std::unique_ptr<MaterialDefinition>
MaterialDefinition::createLambertian(const QString &name,
                                     const glm::vec3 &color) {
  auto mat =
      std::make_unique<MaterialDefinition>(name, MaterialType::Lambertian);
  mat->m_color = color;
  return mat;
}

std::unique_ptr<MaterialDefinition>
MaterialDefinition::createMetal(const QString &name, const glm::vec3 &color,
                                float fuzz) {
  auto mat = std::make_unique<MaterialDefinition>(name, MaterialType::Metal);
  mat->m_color = color;
  mat->m_fuzz = std::clamp(fuzz, 0.0f, 1.0f);
  return mat;
}

std::unique_ptr<MaterialDefinition>
MaterialDefinition::createEmissive(const QString &name, const glm::vec3 &color,
                                   float strength) {
  auto mat = std::make_unique<MaterialDefinition>(name, MaterialType::Emissive);
  mat->m_color = color;
  mat->m_emissiveStrength = std::max(0.0f, strength);
  return mat;
}

std::unique_ptr<MaterialDefinition>
MaterialDefinition::createDielectric(const QString &name, float ior) {
  auto mat =
      std::make_unique<MaterialDefinition>(name, MaterialType::Dielectric);
  mat->m_color = glm::vec3(1.0f, 1.0f, 1.0f); // Glass is clear
  mat->m_refractiveIndex = std::max(1.0f, ior);
  return mat;
}

QString MaterialDefinition::typeToString(MaterialType type) {
  switch (type) {
  case MaterialType::Lambertian:
    return QStringLiteral("Lambertian");
  case MaterialType::Metal:
    return QStringLiteral("Metal");
  case MaterialType::Emissive:
    return QStringLiteral("Emissive");
  case MaterialType::Dielectric:
    return QStringLiteral("Dielectric");
  case MaterialType::PBR:
    return QStringLiteral("PBR");
  }
  return QStringLiteral("Lambertian");
}

MaterialType MaterialDefinition::stringToType(const QString &str) {
  if (str.compare(QLatin1String("Metal"), Qt::CaseInsensitive) == 0)
    return MaterialType::Metal;
  if (str.compare(QLatin1String("Emissive"), Qt::CaseInsensitive) == 0)
    return MaterialType::Emissive;
  if (str.compare(QLatin1String("Dielectric"), Qt::CaseInsensitive) == 0)
    return MaterialType::Dielectric;
  if (str.compare(QLatin1String("PBR"), Qt::CaseInsensitive) == 0)
    return MaterialType::PBR;
  return MaterialType::Lambertian;
}

} // namespace scene
} // namespace Raytracer
