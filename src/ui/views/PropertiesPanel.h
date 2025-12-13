#ifndef RAYTRACER_PROPERTIES_PANEL_H
#define RAYTRACER_PROPERTIES_PANEL_H

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QWidget>
#include <glm/glm.hpp>

namespace Raytracer {
namespace scene {

class SceneDocument;
class SceneNode;
class MaterialDefinition;
class SelectionManager;
class CommandHistory;

/**
 * @brief Vector3 editor widget with X, Y, Z spinboxes
 */
class Vec3Editor : public QWidget {
  Q_OBJECT

public:
  explicit Vec3Editor(const QString &label, QWidget *parent = nullptr);

  void setValue(float x, float y, float z);
  void setValue(const glm::vec3 &v);
  glm::vec3 value() const;

  void setRange(float min, float max);
  void setSingleStep(float step);
  void setDecimals(int decimals);
  void setEnabled(bool enabled);

signals:
  void valueChanged(const glm::vec3 &value);
  void editingFinished();

private:
  QDoubleSpinBox *m_xSpin = nullptr;
  QDoubleSpinBox *m_ySpin = nullptr;
  QDoubleSpinBox *m_zSpin = nullptr;
  bool m_updating = false;
};

/**
 * @brief Color editor widget with color button and RGB values
 */
class ColorEditor : public QWidget {
  Q_OBJECT

public:
  explicit ColorEditor(const QString &label, QWidget *parent = nullptr);

  void setColor(const glm::vec3 &color);
  glm::vec3 color() const;

signals:
  void colorChanged(const glm::vec3 &color);

private slots:
  void onColorButtonClicked();
  void onSpinValueChanged();

private:
  void updateButtonColor();

  QPushButton *m_colorButton = nullptr;
  QDoubleSpinBox *m_rSpin = nullptr;
  QDoubleSpinBox *m_gSpin = nullptr;
  QDoubleSpinBox *m_bSpin = nullptr;
  bool m_updating = false;
};

/**
 * @brief Properties panel for editing selected object properties
 *
 * Shows and edits:
 * - Node name
 * - Transform (position, rotation, scale)
 * - Geometry-specific parameters
 * - Material assignment
 * - Visibility
 */
class PropertiesPanel : public QWidget {
  Q_OBJECT

public:
  explicit PropertiesPanel(QWidget *parent = nullptr);
  ~PropertiesPanel() override = default;

  void setSceneDocument(SceneDocument *document);
  void setSelectionManager(SelectionManager *manager);
  void setCommandHistory(CommandHistory *history);

  SceneDocument *sceneDocument() const { return m_document; }
  SelectionManager *selectionManager() const { return m_selectionManager; }

public slots:
  void refresh();
  void onSelectionChanged();

private slots:
  void onNameChanged();
  void onPositionChanged(const glm::vec3 &pos);
  void onRotationChanged(const glm::vec3 &rot);
  void onScaleChanged(const glm::vec3 &scale);
  void onRadiusChanged(double value);
  void onMaterialChanged(int index);
  void onVisibilityChanged(int state);
  void onV0Changed(const glm::vec3 &v);
  void onV1Changed(const glm::vec3 &v);
  void onV2Changed(const glm::vec3 &v);
  void onMeshFileChanged();

private:
  void setupUI();
  void setupTransformSection();
  void setupGeometrySection();
  void setupMaterialSection();
  void updateFromSelection();
  void updateMaterialCombo();
  void setNoSelection();
  void setSingleSelection(SceneNode *node);
  void setMultiSelection(const std::vector<SceneNode *> &nodes);

  // Helper to get first selected node
  SceneNode *primaryNode() const;

private:
  SceneDocument *m_document = nullptr;
  SelectionManager *m_selectionManager = nullptr;
  CommandHistory *m_commandHistory = nullptr;

  // UI state
  bool m_updating = false;
  bool m_isEditing = false; // Prevents refresh during active transform editing

  // Widgets
  QScrollArea *m_scrollArea = nullptr;
  QWidget *m_contentWidget = nullptr;
  QLabel *m_selectionLabel = nullptr;

  // Node section
  QGroupBox *m_nodeGroup = nullptr;
  QLineEdit *m_nameEdit = nullptr;
  QCheckBox *m_visibleCheck = nullptr;

  // Transform section
  QGroupBox *m_transformGroup = nullptr;
  Vec3Editor *m_positionEditor = nullptr;
  Vec3Editor *m_rotationEditor = nullptr;
  Vec3Editor *m_scaleEditor = nullptr;

  // Geometry section
  QGroupBox *m_geometryGroup = nullptr;
  QLabel *m_geometryTypeLabel = nullptr;

  // Sphere params
  QWidget *m_sphereParams = nullptr;
  QDoubleSpinBox *m_radiusSpin = nullptr;

  // Triangle params
  QWidget *m_triangleParams = nullptr;
  Vec3Editor *m_v0Editor = nullptr;
  Vec3Editor *m_v1Editor = nullptr;
  Vec3Editor *m_v2Editor = nullptr;

  // Mesh params
  QWidget *m_meshParams = nullptr;
  QLineEdit *m_meshFileEdit = nullptr;
  QPushButton *m_browseMeshButton = nullptr;

  // Material section
  QGroupBox *m_materialGroup = nullptr;
  QComboBox *m_materialCombo = nullptr;
  QPushButton *m_editMaterialButton = nullptr;
};

} // namespace scene
} // namespace Raytracer

#endif // RAYTRACER_PROPERTIES_PANEL_H
