#ifndef RAYTRACER_LIGHTING_PANEL_H
#define RAYTRACER_LIGHTING_PANEL_H

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>
#include <glm/glm.hpp>

namespace Raytracer {
namespace scene {

class SceneDocument;
class Vec3Editor;
class ColorEditor;

/**
 * @brief Lighting panel for managing scene lights
 *
 * Provides controls for:
 * - Sun/directional light settings
 * - Point lights list with add/remove
 * - Per-light position, color, intensity
 */
class LightingPanel : public QWidget {
  Q_OBJECT

public:
  explicit LightingPanel(QWidget *parent = nullptr);
  ~LightingPanel() override = default;

  void setSceneDocument(SceneDocument *document);
  SceneDocument *sceneDocument() const { return m_document; }

public slots:
  void refresh();

private slots:
  // Sun settings
  void onSunDirectionChanged(const glm::vec3 &dir);
  void onSunColorChanged(const glm::vec3 &color);
  void onSunIntensityChanged(double value);

  // Point lights
  void onAddPointLight();
  void onRemovePointLight();
  void onPointLightSelected();
  void onPointLightPositionChanged(const glm::vec3 &pos);
  void onPointLightColorChanged(const glm::vec3 &color);
  void onPointLightIntensityChanged(double value);
  void onPointLightEnabledChanged(int state);

private:
  void setupUI();
  void setupSunSection();
  void setupPointLightsSection();
  void updateSunControls();
  void updatePointLightList();
  void updatePointLightControls();
  QUuid selectedPointLightId() const;

private:
  SceneDocument *m_document = nullptr;
  bool m_updating = false;

  // Scroll area
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_contentWidget = nullptr;

  // Sun section
  QGroupBox *m_sunGroup = nullptr;
  Vec3Editor *m_sunDirectionEditor = nullptr;
  ColorEditor *m_sunColorEditor = nullptr;
  QDoubleSpinBox *m_sunIntensitySpin = nullptr;

  // Point lights section
  QGroupBox *m_pointLightsGroup = nullptr;
  QListWidget *m_pointLightsList = nullptr;
  QPushButton *m_addLightButton = nullptr;
  QPushButton *m_removeLightButton = nullptr;

  // Point light properties (for selected light)
  QGroupBox *m_lightPropsGroup = nullptr;
  Vec3Editor *m_lightPositionEditor = nullptr;
  ColorEditor *m_lightColorEditor = nullptr;
  QDoubleSpinBox *m_lightIntensitySpin = nullptr;
  QCheckBox *m_lightEnabledCheck = nullptr;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_LIGHTING_PANEL_H
