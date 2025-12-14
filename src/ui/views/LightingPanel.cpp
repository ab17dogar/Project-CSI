#include "LightingPanel.h"
#include "../models/SceneDocument.h"
#include "PropertiesPanel.h" // For Vec3Editor and ColorEditor

#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace Raytracer {
namespace scene {

LightingPanel::LightingPanel(QWidget *parent) : QWidget(parent) { setupUI(); }

void LightingPanel::setSceneDocument(SceneDocument *document) {
  if (m_document) {
    disconnect(m_document, nullptr, this, nullptr);
  }

  m_document = document;

  if (m_document) {
    connect(m_document, &SceneDocument::lightingChanged, this,
            &LightingPanel::refresh);
    connect(m_document, &SceneDocument::pointLightsChanged, this,
            &LightingPanel::refresh);
  }

  refresh();
}

void LightingPanel::refresh() {
  if (!m_document)
    return;

  m_updating = true;
  updateSunControls();
  updatePointLightList();
  updatePointLightControls();
  m_updating = false;
}

void LightingPanel::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  m_contentWidget = new QWidget();
  auto *contentLayout = new QVBoxLayout(m_contentWidget);
  contentLayout->setContentsMargins(8, 8, 8, 8);
  contentLayout->setSpacing(12);

  setupSunSection();
  contentLayout->addWidget(m_sunGroup);

  setupPointLightsSection();
  contentLayout->addWidget(m_pointLightsGroup);
  contentLayout->addWidget(m_lightPropsGroup);

  contentLayout->addStretch();

  m_scrollArea->setWidget(m_contentWidget);
  mainLayout->addWidget(m_scrollArea);
}

void LightingPanel::setupSunSection() {
  m_sunGroup = new QGroupBox("Sun (Directional Light)", this);
  auto *layout = new QFormLayout(m_sunGroup);

  m_sunDirectionEditor = new Vec3Editor("Direction", this);
  m_sunDirectionEditor->setRange(-10.0, 10.0);
  m_sunDirectionEditor->setSingleStep(0.1);
  connect(m_sunDirectionEditor, &Vec3Editor::valueChanged, this,
          &LightingPanel::onSunDirectionChanged);
  layout->addRow(m_sunDirectionEditor);

  m_sunColorEditor = new ColorEditor("Color", this);
  connect(m_sunColorEditor, &ColorEditor::colorChanged, this,
          &LightingPanel::onSunColorChanged);
  layout->addRow(m_sunColorEditor);

  m_sunIntensitySpin = new QDoubleSpinBox(this);
  m_sunIntensitySpin->setRange(0.0, 10.0);
  m_sunIntensitySpin->setSingleStep(0.1);
  m_sunIntensitySpin->setDecimals(2);
  connect(m_sunIntensitySpin,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &LightingPanel::onSunIntensityChanged);
  layout->addRow("Intensity:", m_sunIntensitySpin);
}

void LightingPanel::setupPointLightsSection() {
  m_pointLightsGroup = new QGroupBox("Point Lights", this);
  auto *layout = new QVBoxLayout(m_pointLightsGroup);

  // List of lights
  m_pointLightsList = new QListWidget(this);
  m_pointLightsList->setMaximumHeight(120);
  connect(m_pointLightsList, &QListWidget::itemSelectionChanged, this,
          &LightingPanel::onPointLightSelected);
  layout->addWidget(m_pointLightsList);

  // Add/Remove buttons
  auto *buttonLayout = new QHBoxLayout();
  m_addLightButton = new QPushButton("Add Light", this);
  m_removeLightButton = new QPushButton("Remove", this);
  connect(m_addLightButton, &QPushButton::clicked, this,
          &LightingPanel::onAddPointLight);
  connect(m_removeLightButton, &QPushButton::clicked, this,
          &LightingPanel::onRemovePointLight);
  buttonLayout->addWidget(m_addLightButton);
  buttonLayout->addWidget(m_removeLightButton);
  layout->addLayout(buttonLayout);

  // Light properties group
  m_lightPropsGroup = new QGroupBox("Light Properties", this);
  auto *propsLayout = new QFormLayout(m_lightPropsGroup);

  m_lightEnabledCheck = new QCheckBox("Enabled", this);
  connect(m_lightEnabledCheck, &QCheckBox::stateChanged, this,
          &LightingPanel::onPointLightEnabledChanged);
  propsLayout->addRow(m_lightEnabledCheck);

  m_lightPositionEditor = new Vec3Editor("Position", this);
  m_lightPositionEditor->setRange(-100.0, 100.0);
  m_lightPositionEditor->setSingleStep(0.1);
  connect(m_lightPositionEditor, &Vec3Editor::valueChanged, this,
          &LightingPanel::onPointLightPositionChanged);
  propsLayout->addRow(m_lightPositionEditor);

  m_lightColorEditor = new ColorEditor("Color", this);
  connect(m_lightColorEditor, &ColorEditor::colorChanged, this,
          &LightingPanel::onPointLightColorChanged);
  propsLayout->addRow(m_lightColorEditor);

  m_lightIntensitySpin = new QDoubleSpinBox(this);
  m_lightIntensitySpin->setRange(0.0, 100.0);
  m_lightIntensitySpin->setSingleStep(0.5);
  m_lightIntensitySpin->setDecimals(1);
  connect(m_lightIntensitySpin,
          QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
          &LightingPanel::onPointLightIntensityChanged);
  propsLayout->addRow("Intensity:", m_lightIntensitySpin);

  m_lightPropsGroup->setEnabled(false);
}

void LightingPanel::updateSunControls() {
  if (!m_document)
    return;

  const auto &sun = m_document->sun();
  m_sunDirectionEditor->setValue(sun.direction);
  m_sunColorEditor->setColor(sun.color);
  m_sunIntensitySpin->setValue(sun.intensity);
}

void LightingPanel::updatePointLightList() {
  if (!m_document)
    return;

  QUuid selectedId = selectedPointLightId();

  m_pointLightsList->clear();
  for (const auto &light : m_document->pointLights()) {
    auto *item = new QListWidgetItem(light.name);
    item->setData(Qt::UserRole, light.uuid);
    m_pointLightsList->addItem(item);

    if (light.uuid == selectedId) {
      item->setSelected(true);
    }
  }

  m_removeLightButton->setEnabled(m_pointLightsList->count() > 0);
}

void LightingPanel::updatePointLightControls() {
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document) {
    m_lightPropsGroup->setEnabled(false);
    return;
  }

  auto *light = m_document->findPointLight(id);
  if (!light) {
    m_lightPropsGroup->setEnabled(false);
    return;
  }

  m_lightPropsGroup->setEnabled(true);
  m_lightEnabledCheck->setChecked(light->enabled);
  m_lightPositionEditor->setValue(light->position);
  m_lightColorEditor->setColor(light->color);
  m_lightIntensitySpin->setValue(light->intensity);
}

QUuid LightingPanel::selectedPointLightId() const {
  auto items = m_pointLightsList->selectedItems();
  if (items.isEmpty())
    return QUuid();
  return items.first()->data(Qt::UserRole).toUuid();
}

// Sun slots
void LightingPanel::onSunDirectionChanged(const glm::vec3 &dir) {
  if (m_updating || !m_document)
    return;
  auto sun = m_document->sun();
  sun.direction = dir;
  m_document->setSun(sun);
}

void LightingPanel::onSunColorChanged(const glm::vec3 &color) {
  if (m_updating || !m_document)
    return;
  auto sun = m_document->sun();
  sun.color = color;
  m_document->setSun(sun);
}

void LightingPanel::onSunIntensityChanged(double value) {
  if (m_updating || !m_document)
    return;
  auto sun = m_document->sun();
  sun.intensity = static_cast<float>(value);
  m_document->setSun(sun);
}

// Point light slots
void LightingPanel::onAddPointLight() {
  if (!m_document)
    return;

  PointLightSettings light;
  light.name =
      QString("Point Light %1").arg(m_document->pointLights().size() + 1);
  light.position = glm::vec3(0.0f, 1.0f, 0.0f);
  light.color = glm::vec3(1.0f, 0.95f, 0.8f);
  light.intensity = 5.0f;

  m_document->addPointLight(light);
}

void LightingPanel::onRemovePointLight() {
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document)
    return;
  m_document->removePointLight(id);
}

void LightingPanel::onPointLightSelected() { updatePointLightControls(); }

void LightingPanel::onPointLightPositionChanged(const glm::vec3 &pos) {
  if (m_updating)
    return;
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document)
    return;
  auto *light = m_document->findPointLight(id);
  if (light) {
    PointLightSettings updated = *light;
    updated.position = pos;
    m_document->updatePointLight(id, updated);
  }
}

void LightingPanel::onPointLightColorChanged(const glm::vec3 &color) {
  if (m_updating)
    return;
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document)
    return;
  auto *light = m_document->findPointLight(id);
  if (light) {
    PointLightSettings updated = *light;
    updated.color = color;
    m_document->updatePointLight(id, updated);
  }
}

void LightingPanel::onPointLightIntensityChanged(double value) {
  if (m_updating)
    return;
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document)
    return;
  auto *light = m_document->findPointLight(id);
  if (light) {
    PointLightSettings updated = *light;
    updated.intensity = static_cast<float>(value);
    m_document->updatePointLight(id, updated);
  }
}

void LightingPanel::onPointLightEnabledChanged(int state) {
  if (m_updating)
    return;
  QUuid id = selectedPointLightId();
  if (id.isNull() || !m_document)
    return;
  auto *light = m_document->findPointLight(id);
  if (light) {
    PointLightSettings updated = *light;
    updated.enabled = (state == Qt::Checked);
    m_document->updatePointLight(id, updated);
  }
}

} // namespace scene
} // namespace Raytracer
