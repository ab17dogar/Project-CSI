#pragma once

#include <QUuid>
#include <QWidget>

#include "ui/models/RenderQueueModel.h"

class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;
class QListWidget;
class QPushButton;
class QListWidgetItem;
class QCheckBox;
class QComboBox;
class QTreeWidget;

namespace Raytracer {
namespace ui {

class RenderSettingsModel;
class RenderController;
// RenderQueueModel included above

class PresetRepository;

class RenderSettingsPanel : public QWidget {
  Q_OBJECT

public:
  explicit RenderSettingsPanel(RenderSettingsModel &model,
                               RenderController &controller,
                               PresetRepository &repository,
                               QWidget *parent = nullptr);

signals:
  void importXmlRequested(const QString &path);
  void importObjRequested(const QString &path);
  void importMtlRequested(const QString &path);

private slots:
  void syncFromModel();
  void syncPresetUi();
  void handleAddToQueue();
  void handleStartQueue();
  void handleClearQueue();
  void handleRemoveSelectedJob();
  void handleRetrySelectedJob();
  void onQueueChanged();
  void onQueueProcessingChanged(bool running);
  void onActiveQueueJobChanged(const RenderQueueModel::QueuedRender &job);
  void onQueueDrained();
  void handlePresetSelectionChanged();
  void handlePresetActivated(QListWidgetItem *item);
  void handleSavePreset();
  void handleDeletePreset();
  void handleImportPresets();
  void handleExportPresets();
  void handleToneMappingChanged(int index);
  void handleRemoteToggleChanged(bool enabled);
  void handleRemoteWorkerChanged(int index);
  void handleApplyJobDispatch();
  void handleCancelRemoteJob();
  void handleManageCredentials();
  void handleImportXml();
  void handleImportObj();
  void handleImportMtl();
  void handleAccelerationChanged(int index);

private:
  void buildUi();
  void connectSignals();
  void buildPresetControls(class QVBoxLayout *parentLayout);
  void buildImportAssetsControls(QVBoxLayout *parentLayout);
  void buildPostProcessingControls(QVBoxLayout *parentLayout);
  void buildQueueControls(QVBoxLayout *parentLayout);
  void buildWorkerStatusPanel(QVBoxLayout *parentLayout);
  void handleBrowseOutput();
  void refreshQueueList();
  void refreshPresetList();
  void updateQueueControls();
  void updatePresetControls();
  void updatePresetStatusLabel();
  void syncPostProcessingUi();
  void syncRemoteSettingsUi();
  void populateRemoteWorkers();
  void
  updateRemoteQueueStatus(const RenderQueueModel::QueuedRender *job = nullptr);
  void refreshWorkerStatus();
  void syncSelectedJobDispatch();
  void updateToneMappingDescription();
  QString describeJob(const RenderQueueModel::QueuedRender &job) const;
  QString selectedPresetName() const;
  bool canDeleteSelectedPreset() const;
  QUuid selectedJobId() const;

private:
  RenderSettingsModel &m_model;
  RenderController &m_controller;
  RenderQueueModel &m_queueModel;
  PresetRepository &m_presetRepository;
  QSpinBox *m_widthSpin{nullptr};
  QSpinBox *m_heightSpin{nullptr};
  QSpinBox *m_samplesSpin{nullptr};
  QLabel *m_scenePathLabel{nullptr};
  QListWidget *m_presetList{nullptr};
  QLabel *m_presetStatusLabel{nullptr};
  QListWidget *m_queueList{nullptr};
  QPushButton *m_queueAddButton{nullptr};
  QPushButton *m_queueStartButton{nullptr};
  QPushButton *m_queueClearButton{nullptr};
  QPushButton *m_queueRemoveButton{nullptr};
  QPushButton *m_queueRetryButton{nullptr};
  QPushButton *m_queueBrowseButton{nullptr};
  QLineEdit *m_queueOutputEdit{nullptr};
  QLabel *m_queueStatusLabel{nullptr};
  QLabel *m_remoteJobStatusLabel{nullptr};
  QPushButton *m_remoteCancelButton{nullptr};
  QPushButton *m_presetApplyButton{nullptr};
  QPushButton *m_presetSaveButton{nullptr};
  QPushButton *m_presetDeleteButton{nullptr};
  QPushButton *m_presetImportButton{nullptr};
  QPushButton *m_presetExportButton{nullptr};
  QCheckBox *m_denoiserCheck{nullptr};
  QComboBox *m_toneMappingCombo{nullptr};
  QLabel *m_toneMappingDescription{nullptr};
  QCheckBox *m_remoteDispatchCheck{nullptr};
  QComboBox *m_remoteWorkerCombo{nullptr};
  QComboBox *m_jobDispatchCombo{nullptr};
  QComboBox *m_jobWorkerCombo{nullptr};
  QPushButton *m_jobDispatchApplyButton{nullptr};
  QPushButton *m_manageCredentialsButton{nullptr};
  QPushButton *m_importXmlButton{nullptr};
  QPushButton *m_importObjButton{nullptr};
  QPushButton *m_importMtlButton{nullptr};
  QPushButton *m_sceneBrowseButton{nullptr};
  QComboBox *m_accelerationCombo{nullptr};
  class QTreeWidget *m_workerStatusView{nullptr};
  bool m_queueProcessing{false};
  bool m_activeJobIsRemote{false};
  QUuid m_activeJobId;
};

} // namespace ui
} // namespace Raytracer
