#include "PropertiesPanel.h"
#include "../commands/CommandHistory.h"
#include "../commands/SceneCommands.h"
#include "../controllers/SelectionManager.h"
#include "../models/MaterialDefinition.h"
#include "../models/SceneDocument.h"
#include "../models/SceneNode.h"

#include <QColorDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace Raytracer {
namespace scene {

// ===== Vec3Editor =====

Vec3Editor::Vec3Editor(const QString &label, QWidget *parent)
    : QWidget(parent) {
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  if (!label.isEmpty()) {
    QLabel *lbl = new QLabel(label, this);
    lbl->setMinimumWidth(60);
    layout->addWidget(lbl);
  }

  auto createSpin = [this]() {
    QDoubleSpinBox *spin = new QDoubleSpinBox(this);
    spin->setRange(-10000, 10000);
    spin->setDecimals(3);
    spin->setSingleStep(0.1);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this]() {
              if (!m_updating) {
                emit valueChanged(value());
              }
            });
    connect(spin, &QDoubleSpinBox::editingFinished, this,
            &Vec3Editor::editingFinished);
    return spin;
  };

  layout->addWidget(new QLabel("X:", this));
  m_xSpin = createSpin();
  layout->addWidget(m_xSpin);

  layout->addWidget(new QLabel("Y:", this));
  m_ySpin = createSpin();
  layout->addWidget(m_ySpin);

  layout->addWidget(new QLabel("Z:", this));
  m_zSpin = createSpin();
  layout->addWidget(m_zSpin);
}

void Vec3Editor::setValue(float x, float y, float z) {
  m_updating = true;
  m_xSpin->setValue(x);
  m_ySpin->setValue(y);
  m_zSpin->setValue(z);
  m_updating = false;
}

void Vec3Editor::setValue(const glm::vec3 &v) { setValue(v.x, v.y, v.z); }

glm::vec3 Vec3Editor::value() const {
  return glm::vec3(m_xSpin->value(), m_ySpin->value(), m_zSpin->value());
}

void Vec3Editor::setRange(float min, float max) {
  m_xSpin->setRange(min, max);
  m_ySpin->setRange(min, max);
  m_zSpin->setRange(min, max);
}

void Vec3Editor::setSingleStep(float step) {
  m_xSpin->setSingleStep(step);
  m_ySpin->setSingleStep(step);
  m_zSpin->setSingleStep(step);
}

void Vec3Editor::setDecimals(int decimals) {
  m_xSpin->setDecimals(decimals);
  m_ySpin->setDecimals(decimals);
  m_zSpin->setDecimals(decimals);
}

void Vec3Editor::setEnabled(bool enabled) {
  QWidget::setEnabled(enabled);
  m_xSpin->setEnabled(enabled);
  m_ySpin->setEnabled(enabled);
  m_zSpin->setEnabled(enabled);
}

// ===== ColorEditor =====

ColorEditor::ColorEditor(const QString &label, QWidget *parent)
    : QWidget(parent) {
  QHBoxLayout *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(4);

  if (!label.isEmpty()) {
    QLabel *lbl = new QLabel(label, this);
    lbl->setMinimumWidth(60);
    layout->addWidget(lbl);
  }

  m_colorButton = new QPushButton(this);
  m_colorButton->setFixedSize(24, 24);
  connect(m_colorButton, &QPushButton::clicked, this,
          &ColorEditor::onColorButtonClicked);
  layout->addWidget(m_colorButton);

  auto createSpin = [this]() {
    QDoubleSpinBox *spin = new QDoubleSpinBox(this);
    spin->setRange(0, 1);
    spin->setDecimals(3);
    spin->setSingleStep(0.05);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            &ColorEditor::onSpinValueChanged);
    return spin;
  };

  layout->addWidget(new QLabel("R:", this));
  m_rSpin = createSpin();
  layout->addWidget(m_rSpin);

  layout->addWidget(new QLabel("G:", this));
  m_gSpin = createSpin();
  layout->addWidget(m_gSpin);

  layout->addWidget(new QLabel("B:", this));
  m_bSpin = createSpin();
  layout->addWidget(m_bSpin);

  updateButtonColor();
}

void ColorEditor::setColor(const glm::vec3 &color) {
  m_updating = true;
  m_rSpin->setValue(color.r);
  m_gSpin->setValue(color.g);
  m_bSpin->setValue(color.b);
  m_updating = false;
  updateButtonColor();
}

glm::vec3 ColorEditor::color() const {
  return glm::vec3(m_rSpin->value(), m_gSpin->value(), m_bSpin->value());
}

void ColorEditor::onColorButtonClicked() {
  QColor current =
      QColor::fromRgbF(m_rSpin->value(), m_gSpin->value(), m_bSpin->value());
  QColor selected = QColorDialog::getColor(current, this, tr("Select Color"));

  if (selected.isValid()) {
    setColor(glm::vec3(selected.redF(), selected.greenF(), selected.blueF()));
    emit colorChanged(color());
  }
}

void ColorEditor::onSpinValueChanged() {
  if (!m_updating) {
    updateButtonColor();
    emit colorChanged(color());
  }
}

void ColorEditor::updateButtonColor() {
  QColor c =
      QColor::fromRgbF(m_rSpin->value(), m_gSpin->value(), m_bSpin->value());
  m_colorButton->setStyleSheet(
      QStringLiteral("background-color: %1; border: 1px solid gray;")
          .arg(c.name()));
}

// ===== PropertiesPanel =====

PropertiesPanel::PropertiesPanel(QWidget *parent) : QWidget(parent) {
  setupUI();
}

void PropertiesPanel::setupUI() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  // Selection info label
  m_selectionLabel = new QLabel(tr("No Selection"), this);
  m_selectionLabel->setStyleSheet("font-weight: bold; padding: 8px;");
  mainLayout->addWidget(m_selectionLabel);

  // Scroll area for properties
  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setWidgetResizable(true);
  m_scrollArea->setFrameShape(QFrame::NoFrame);
  mainLayout->addWidget(m_scrollArea);

  m_contentWidget = new QWidget(m_scrollArea);
  QVBoxLayout *contentLayout = new QVBoxLayout(m_contentWidget);
  contentLayout->setContentsMargins(8, 8, 8, 8);
  contentLayout->setSpacing(8);

  // Node properties group
  m_nodeGroup = new QGroupBox(tr("Node"), m_contentWidget);
  QFormLayout *nodeLayout = new QFormLayout(m_nodeGroup);

  m_nameEdit = new QLineEdit(m_nodeGroup);
  connect(m_nameEdit, &QLineEdit::editingFinished, this,
          &PropertiesPanel::onNameChanged);
  nodeLayout->addRow(tr("Name:"), m_nameEdit);

  m_visibleCheck = new QCheckBox(tr("Visible"), m_nodeGroup);
  connect(m_visibleCheck, &QCheckBox::stateChanged, this,
          &PropertiesPanel::onVisibilityChanged);
  nodeLayout->addRow(m_visibleCheck);

  contentLayout->addWidget(m_nodeGroup);

  // Transform group
  setupTransformSection();
  contentLayout->addWidget(m_transformGroup);

  // Geometry group
  setupGeometrySection();
  contentLayout->addWidget(m_geometryGroup);

  // Material group
  setupMaterialSection();
  contentLayout->addWidget(m_materialGroup);

  // Spacer
  contentLayout->addStretch();

  m_scrollArea->setWidget(m_contentWidget);

  setNoSelection();
}

void PropertiesPanel::setupTransformSection() {
  m_transformGroup = new QGroupBox(tr("Transform"), m_contentWidget);
  QVBoxLayout *layout = new QVBoxLayout(m_transformGroup);

  m_positionEditor = new Vec3Editor(tr("Position"), m_transformGroup);
  connect(m_positionEditor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onPositionChanged);
  layout->addWidget(m_positionEditor);

  m_rotationEditor = new Vec3Editor(tr("Rotation"), m_transformGroup);
  m_rotationEditor->setRange(-360, 360);
  m_rotationEditor->setSingleStep(5);
  connect(m_rotationEditor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onRotationChanged);
  layout->addWidget(m_rotationEditor);

  m_scaleEditor = new Vec3Editor(tr("Scale"), m_transformGroup);
  m_scaleEditor->setRange(0.001, 1000);
  m_scaleEditor->setSingleStep(0.1);
  connect(m_scaleEditor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onScaleChanged);
  layout->addWidget(m_scaleEditor);
}

void PropertiesPanel::setupGeometrySection() {
  m_geometryGroup = new QGroupBox(tr("Geometry"), m_contentWidget);
  QVBoxLayout *layout = new QVBoxLayout(m_geometryGroup);

  m_geometryTypeLabel = new QLabel(m_geometryGroup);
  layout->addWidget(m_geometryTypeLabel);

  // Sphere parameters
  m_sphereParams = new QWidget(m_geometryGroup);
  QFormLayout *sphereLayout = new QFormLayout(m_sphereParams);
  sphereLayout->setContentsMargins(0, 0, 0, 0);

  m_radiusSpin = new QDoubleSpinBox(m_sphereParams);
  m_radiusSpin->setRange(0.001, 1000);
  m_radiusSpin->setDecimals(3);
  m_radiusSpin->setSingleStep(0.1);
  connect(m_radiusSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &PropertiesPanel::onRadiusChanged);
  sphereLayout->addRow(tr("Radius:"), m_radiusSpin);

  layout->addWidget(m_sphereParams);

  // Triangle parameters
  m_triangleParams = new QWidget(m_geometryGroup);
  QVBoxLayout *triangleLayout = new QVBoxLayout(m_triangleParams);
  triangleLayout->setContentsMargins(0, 0, 0, 0);

  m_v0Editor = new Vec3Editor(tr("V0"), m_triangleParams);
  connect(m_v0Editor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onV0Changed);
  triangleLayout->addWidget(m_v0Editor);

  m_v1Editor = new Vec3Editor(tr("V1"), m_triangleParams);
  connect(m_v1Editor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onV1Changed);
  triangleLayout->addWidget(m_v1Editor);

  m_v2Editor = new Vec3Editor(tr("V2"), m_triangleParams);
  connect(m_v2Editor, &Vec3Editor::valueChanged, this,
          &PropertiesPanel::onV2Changed);
  triangleLayout->addWidget(m_v2Editor);

  layout->addWidget(m_triangleParams);

  // Mesh parameters
  m_meshParams = new QWidget(m_geometryGroup);
  QHBoxLayout *meshLayout = new QHBoxLayout(m_meshParams);
  meshLayout->setContentsMargins(0, 0, 0, 0);

  meshLayout->addWidget(new QLabel(tr("File:"), m_meshParams));
  m_meshFileEdit = new QLineEdit(m_meshParams);
  connect(m_meshFileEdit, &QLineEdit::editingFinished, this,
          &PropertiesPanel::onMeshFileChanged);
  meshLayout->addWidget(m_meshFileEdit);

  m_browseMeshButton = new QPushButton(tr("..."), m_meshParams);
  m_browseMeshButton->setMaximumWidth(30);
  connect(m_browseMeshButton, &QPushButton::clicked, this, [this]() {
    QString file = QFileDialog::getOpenFileName(
        this, tr("Select Mesh File"), QString(), tr("OBJ Files (*.obj)"));
    if (!file.isEmpty()) {
      m_meshFileEdit->setText(file);
      onMeshFileChanged();
    }
  });
  meshLayout->addWidget(m_browseMeshButton);

  layout->addWidget(m_meshParams);

  // Hide all by default
  m_sphereParams->hide();
  m_triangleParams->hide();
  m_meshParams->hide();
}

void PropertiesPanel::setupMaterialSection() {
  m_materialGroup = new QGroupBox(tr("Material"), m_contentWidget);
  QHBoxLayout *layout = new QHBoxLayout(m_materialGroup);

  m_materialCombo = new QComboBox(m_materialGroup);
  connect(m_materialCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &PropertiesPanel::onMaterialChanged);
  layout->addWidget(m_materialCombo, 1);

  m_editMaterialButton = new QPushButton(tr("Edit..."), m_materialGroup);
  layout->addWidget(m_editMaterialButton);

  // Note: Built-in material presets are added in updateMaterialCombo()
}

void PropertiesPanel::setSceneDocument(SceneDocument *document) {
  if (m_document) {
    disconnect(m_document, nullptr, this, nullptr);
  }

  m_document = document;

  if (m_document) {
    connect(m_document, &SceneDocument::documentChanged, this,
            &PropertiesPanel::refresh);
    connect(m_document, &SceneDocument::materialAdded, this,
            &PropertiesPanel::updateMaterialCombo);
    connect(m_document, &SceneDocument::materialRemoved, this,
            &PropertiesPanel::updateMaterialCombo);
  }

  updateMaterialCombo();
  refresh();
}

void PropertiesPanel::setSelectionManager(SelectionManager *manager) {
  if (m_selectionManager) {
    disconnect(m_selectionManager, nullptr, this, nullptr);
  }

  m_selectionManager = manager;

  if (m_selectionManager) {
    connect(m_selectionManager, &SelectionManager::selectionChanged, this,
            &PropertiesPanel::onSelectionChanged);
  }

  onSelectionChanged();
}

void PropertiesPanel::setCommandHistory(CommandHistory *history) {
  m_commandHistory = history;
}

void PropertiesPanel::refresh() { updateFromSelection(); }

void PropertiesPanel::onSelectionChanged() { updateFromSelection(); }

void PropertiesPanel::updateFromSelection() {
  if (!m_selectionManager) {
    setNoSelection();
    return;
  }

  auto selected = m_selectionManager->selectedNodes();

  if (selected.empty()) {
    setNoSelection();
  } else if (selected.size() == 1) {
    setSingleSelection(selected[0]);
  } else {
    setMultiSelection(selected);
  }
}

void PropertiesPanel::updateMaterialCombo() {
  m_updating = true;
  m_materialCombo->clear();

  // First item is "None"
  m_materialCombo->addItem(tr("(None)"), QString());

  // Add all materials from the document (includes default presets)
  if (m_document) {
    for (const auto &mat : m_document->materials()) {
      m_materialCombo->addItem(mat->name(), mat->uuid().toString());
    }
  }

  m_updating = false;
}

void PropertiesPanel::setNoSelection() {
  m_selectionLabel->setText(tr("No Selection"));
  m_nodeGroup->setEnabled(false);
  m_transformGroup->setEnabled(false);
  m_geometryGroup->setEnabled(false);
  m_materialGroup->setEnabled(false);

  m_updating = true;
  m_nameEdit->clear();
  m_visibleCheck->setChecked(false);
  m_positionEditor->setValue(0, 0, 0);
  m_rotationEditor->setValue(0, 0, 0);
  m_scaleEditor->setValue(1, 1, 1);
  m_updating = false;
}

void PropertiesPanel::setSingleSelection(SceneNode *node) {
  // Don't refresh transform editors while user is actively editing
  if (m_isEditing)
    return;

  if (!node) {
    setNoSelection();
    return;
  }

  m_updating = true;

  m_selectionLabel->setText(node->name());
  m_nodeGroup->setEnabled(true);
  m_transformGroup->setEnabled(true);
  m_geometryGroup->setEnabled(true);
  m_materialGroup->setEnabled(true);

  // Node properties
  m_nameEdit->setText(node->name());
  m_visibleCheck->setChecked(node->isVisible());

  // Transform
  m_positionEditor->setValue(node->transform()->position());
  m_rotationEditor->setValue(node->transform()->rotation());
  m_scaleEditor->setValue(node->transform()->scale());

  // Geometry
  m_sphereParams->hide();
  m_triangleParams->hide();
  m_meshParams->hide();

  switch (node->geometryType()) {
  case GeometryType::Sphere:
    m_geometryTypeLabel->setText(tr("Type: Sphere"));
    m_sphereParams->show();
    m_radiusSpin->setValue(node->geometryParams().radius);
    break;

  case GeometryType::Triangle:
    m_geometryTypeLabel->setText(tr("Type: Triangle"));
    m_triangleParams->show();
    m_v0Editor->setValue(node->geometryParams().v0);
    m_v1Editor->setValue(node->geometryParams().v1);
    m_v2Editor->setValue(node->geometryParams().v2);
    break;

  case GeometryType::Mesh:
    m_geometryTypeLabel->setText(tr("Type: Mesh"));
    m_meshParams->show();
    m_meshFileEdit->setText(node->geometryParams().meshFilePath);
    break;

  case GeometryType::None:
    m_geometryTypeLabel->setText(tr("Type: Group"));
    break;

  default:
    m_geometryTypeLabel->setText(tr("Type: Unknown"));
    break;
  }

  // Material
  int matIndex = 0;
  if (!node->materialId().isNull()) {
    for (int i = 1; i < m_materialCombo->count(); ++i) {
      if (m_materialCombo->itemData(i).toString() ==
          node->materialId().toString()) {
        matIndex = i;
        break;
      }
    }
  }
  m_materialCombo->setCurrentIndex(matIndex);

  m_updating = false;
}

void PropertiesPanel::setMultiSelection(const std::vector<SceneNode *> &nodes) {
  m_selectionLabel->setText(tr("%1 objects selected").arg(nodes.size()));

  // For multi-selection, show limited editing
  m_nodeGroup->setEnabled(false);
  m_transformGroup->setEnabled(true); // Allow batch transform
  m_geometryGroup->setEnabled(false);
  m_materialGroup->setEnabled(true); // Allow batch material assignment

  // TODO: Show common values or placeholders
}

SceneNode *PropertiesPanel::primaryNode() const {
  if (!m_selectionManager)
    return nullptr;
  return m_selectionManager->primarySelection();
}

void PropertiesPanel::onNameChanged() {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  QString newName = m_nameEdit->text().trimmed();
  if (newName.isEmpty() || newName == node->name())
    return;

  if (m_commandHistory) {
    auto cmd = std::make_unique<SetNodeNameCommand>(m_document, node, newName);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setName(newName);
  }
}

void PropertiesPanel::onPositionChanged(const glm::vec3 &pos) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  m_isEditing = true;
  if (m_commandHistory) {
    auto cmd = std::make_unique<SetPositionCommand>(m_document, node, pos);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->transform()->setPosition(pos);
  }
  m_isEditing = false;
}

void PropertiesPanel::onRotationChanged(const glm::vec3 &rot) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  m_isEditing = true;
  if (m_commandHistory) {
    auto cmd = std::make_unique<TransformCommand>(
        m_document, node, node->transform()->position(), rot,
        node->transform()->scale());
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->transform()->setRotation(rot);
  }
  m_isEditing = false;
}

void PropertiesPanel::onScaleChanged(const glm::vec3 &scale) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  m_isEditing = true;
  if (m_commandHistory) {
    auto cmd = std::make_unique<TransformCommand>(
        m_document, node, node->transform()->position(),
        node->transform()->rotation(), scale);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->transform()->setScale(scale);
  }
  m_isEditing = false;
}

void PropertiesPanel::onRadiusChanged(double value) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  GeometryParams params = node->geometryParams();
  params.radius = static_cast<float>(value);

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetGeometryParamsCommand>(m_document, node, params);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setGeometryParams(params);
  }
}

void PropertiesPanel::onMaterialChanged(int index) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  QUuid matId;
  if (index > 0) {
    matId = QUuid(m_materialCombo->itemData(index).toString());
  }

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetNodeMaterialCommand>(m_document, node, matId);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setMaterialId(matId);
  }
}

void PropertiesPanel::onVisibilityChanged(int state) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  bool visible = (state == Qt::Checked);

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetNodeVisibilityCommand>(m_document, node, visible);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setVisible(visible);
  }
}

void PropertiesPanel::onV0Changed(const glm::vec3 &v) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  GeometryParams params = node->geometryParams();
  params.v0 = v;

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetGeometryParamsCommand>(m_document, node, params);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setGeometryParams(params);
  }
}

void PropertiesPanel::onV1Changed(const glm::vec3 &v) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  GeometryParams params = node->geometryParams();
  params.v1 = v;

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetGeometryParamsCommand>(m_document, node, params);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setGeometryParams(params);
  }
}

void PropertiesPanel::onV2Changed(const glm::vec3 &v) {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  GeometryParams params = node->geometryParams();
  params.v2 = v;

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetGeometryParamsCommand>(m_document, node, params);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setGeometryParams(params);
  }
}

void PropertiesPanel::onMeshFileChanged() {
  if (m_updating)
    return;

  SceneNode *node = primaryNode();
  if (!node || !m_document)
    return;

  GeometryParams params = node->geometryParams();
  params.meshFilePath = m_meshFileEdit->text();

  if (m_commandHistory) {
    auto cmd =
        std::make_unique<SetGeometryParamsCommand>(m_document, node, params);
    m_commandHistory->execute(std::move(cmd));
  } else {
    node->setGeometryParams(params);
  }
}

} // namespace scene
} // namespace Raytracer
