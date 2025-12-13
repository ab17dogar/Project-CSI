#include "ui/views/RenderSettingsPanel.h"

#include "ui/controllers/RenderController.h"
#include "ui/models/PresetRepository.h"
#include "ui/models/RenderQueueModel.h"
#include "ui/models/RenderSettingsModel.h"
#include "ui/services/remote/RemoteWorkerConfig.h"
#include "ui/views/PresetEditorDialog.h"
#include "ui/views/RemoteCredentialDialog.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>

#include "ui/rendering/ToneMappingPresets.h"
namespace Raytracer {
namespace ui {

RenderSettingsPanel::RenderSettingsPanel(RenderSettingsModel &model,
                                         RenderController &controller,
                                         PresetRepository &repository,
                                         QWidget *parent)
    : QWidget(parent), m_model(model), m_controller(controller),
      m_queueModel(controller.queueModel()), m_presetRepository(repository) {
  buildUi();
  connectSignals();
  populateRemoteWorkers();
  refreshPresetList();
  syncFromModel();
  refreshQueueList();
  updateQueueControls();
  updatePresetControls();
}

void RenderSettingsPanel::buildUi() {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(8, 8, 8, 8);

  auto *group = new QGroupBox(tr("Render Settings"), this);
  auto *form = new QFormLayout(group);

  m_widthSpin = new QSpinBox(group);
  m_widthSpin->setRange(16, 16384);
  m_widthSpin->setSingleStep(16);
  form->addRow(tr("Width"), m_widthSpin);

  m_heightSpin = new QSpinBox(group);
  m_heightSpin->setRange(16, 16384);
  m_heightSpin->setSingleStep(16);
  form->addRow(tr("Height"), m_heightSpin);

  m_samplesSpin = new QSpinBox(group);
  m_samplesSpin->setRange(1, 4096);
  m_samplesSpin->setSingleStep(1);
  form->addRow(tr("Samples"), m_samplesSpin);

  // Acceleration method combo box
  m_accelerationCombo = new QComboBox(group);
  m_accelerationCombo->addItem(tr("Linear (Default)"), 0);
  m_accelerationCombo->addItem(tr("BVH Accelerated"), 1);
  m_accelerationCombo->setToolTip(
      tr("Linear: Tests all objects sequentially\nBVH: Uses Bounding Volume "
         "Hierarchy for faster rendering with many objects"));
  form->addRow(tr("Acceleration"), m_accelerationCombo);

  // Scene path with browse button
  auto *sceneRow = new QHBoxLayout();
  m_scenePathLabel = new QLabel(group);
  m_scenePathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                            Qt::TextSelectableByKeyboard);
  m_scenePathLabel->setStyleSheet(
      QStringLiteral("QLabel { background-color: #2a2a2a; padding: 4px 8px; "
                     "border-radius: 3px; color: #ddd; }"));
  m_scenePathLabel->setMinimumWidth(150);
  sceneRow->addWidget(m_scenePathLabel, 1);

  m_sceneBrowseButton = new QPushButton(tr("📂"), group);
  m_sceneBrowseButton->setFixedWidth(32);
  m_sceneBrowseButton->setToolTip(tr("Browse for scene file"));
  m_sceneBrowseButton->setCursor(Qt::PointingHandCursor);
  sceneRow->addWidget(m_sceneBrowseButton);
  form->addRow(tr("Scene"), sceneRow);

  group->setLayout(form);
  layout->addWidget(group);
  buildImportAssetsControls(layout);
  buildPresetControls(layout);
  // buildPostProcessingControls(layout);
  // buildQueueControls(layout);
  // buildWorkerStatusPanel(layout);
  layout->addStretch();
}

void RenderSettingsPanel::connectSignals() {
  connect(m_widthSpin, &QSpinBox::valueChanged, &m_model,
          &RenderSettingsModel::setWidth);
  connect(m_heightSpin, &QSpinBox::valueChanged, &m_model,
          &RenderSettingsModel::setHeight);
  connect(m_samplesSpin, &QSpinBox::valueChanged, &m_model,
          &RenderSettingsModel::setSamples);

  connect(&m_model, &RenderSettingsModel::settingsChanged, this,
          &RenderSettingsPanel::syncFromModel);
  connect(&m_model, &RenderSettingsModel::scenePathChanged, this,
          &RenderSettingsPanel::syncFromModel);
  connect(&m_model, &RenderSettingsModel::presetChanged, this,
          &RenderSettingsPanel::syncPresetUi);
  connect(&m_presetRepository, &PresetRepository::presetsChanged, this,
          &RenderSettingsPanel::refreshPresetList);
  connect(&m_model, &RenderSettingsModel::postProcessingChanged, this,
          &RenderSettingsPanel::syncPostProcessingUi);
  connect(&m_model, &RenderSettingsModel::remoteSettingsChanged, this,
          &RenderSettingsPanel::syncRemoteSettingsUi);

  if (m_denoiserCheck) {
    connect(m_denoiserCheck, &QCheckBox::toggled, &m_model,
            &RenderSettingsModel::setDenoiserEnabled);
  }
  if (m_toneMappingCombo) {
    connect(m_toneMappingCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RenderSettingsPanel::handleToneMappingChanged);
  }
  if (m_remoteDispatchCheck) {
    connect(m_remoteDispatchCheck, &QCheckBox::toggled, this,
            &RenderSettingsPanel::handleRemoteToggleChanged);
  }
  if (m_remoteWorkerCombo) {
    connect(m_remoteWorkerCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RenderSettingsPanel::handleRemoteWorkerChanged);
  }
  if (m_manageCredentialsButton) {
    connect(m_manageCredentialsButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleManageCredentials);
  }
  if (m_importXmlButton) {
    connect(m_importXmlButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleImportXml);
  }
  if (m_sceneBrowseButton) {
    connect(m_sceneBrowseButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleImportXml);
  }
  if (m_importObjButton) {
    connect(m_importObjButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleImportObj);
  }
  if (m_importMtlButton) {
    connect(m_importMtlButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleImportMtl);
  }
  if (m_accelerationCombo) {
    connect(m_accelerationCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &RenderSettingsPanel::handleAccelerationChanged);
  }

  if (m_presetList) {
    connect(m_presetList, &QListWidget::itemSelectionChanged, this,
            &RenderSettingsPanel::handlePresetSelectionChanged);
    connect(m_presetList, &QListWidget::itemDoubleClicked, this,
            &RenderSettingsPanel::handlePresetActivated);
  }
  if (m_presetApplyButton) {
    connect(m_presetApplyButton, &QPushButton::clicked, this, [this]() {
      const QString name = selectedPresetName();
      if (!name.isEmpty()) {
        m_model.setPreset(name);
      }
    });
  }
  if (m_presetSaveButton) {
    connect(m_presetSaveButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleSavePreset);
  }
  if (m_presetDeleteButton) {
    connect(m_presetDeleteButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleDeletePreset);
  }
  if (m_presetImportButton) {
    connect(m_presetImportButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleImportPresets);
  }
  if (m_presetExportButton) {
    connect(m_presetExportButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleExportPresets);
  }

  if (m_queueAddButton) {
    connect(m_queueAddButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleAddToQueue);
  }
  if (m_queueStartButton) {
    connect(m_queueStartButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleStartQueue);
  }
  if (m_queueClearButton) {
    connect(m_queueClearButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleClearQueue);
  }
  if (m_queueRemoveButton) {
    connect(m_queueRemoveButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleRemoveSelectedJob);
  }
  if (m_queueRetryButton) {
    connect(m_queueRetryButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleRetrySelectedJob);
  }
  if (m_queueList) {
    connect(m_queueList, &QListWidget::itemSelectionChanged, this, [this]() {
      updateQueueControls();
      syncSelectedJobDispatch();
    });
  }
  if (m_queueBrowseButton) {
    connect(m_queueBrowseButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleBrowseOutput);
  }

  if (m_jobDispatchCombo) {
    connect(m_jobDispatchCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
              const bool remoteSelected =
                  m_jobDispatchCombo->itemData(index).toInt() ==
                  static_cast<int>(RenderQueueModel::DispatchTarget::Remote);
              if (m_jobWorkerCombo) {
                const bool enableWorker = remoteSelected &&
                                          !m_queueProcessing &&
                                          !selectedJobId().isNull();
                m_jobWorkerCombo->setEnabled(enableWorker);
              }
              if (m_jobDispatchApplyButton) {
                const bool enableApply =
                    !selectedJobId().isNull() && !m_queueProcessing;
                m_jobDispatchApplyButton->setEnabled(enableApply);
              }
            });
  }
  if (m_jobWorkerCombo) {
    connect(m_jobWorkerCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this]() {
              if (m_jobDispatchApplyButton) {
                const bool enableApply =
                    !selectedJobId().isNull() && !m_queueProcessing;
                m_jobDispatchApplyButton->setEnabled(enableApply);
              }
            });
  }
  if (m_jobDispatchApplyButton) {
    connect(m_jobDispatchApplyButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleApplyJobDispatch);
  }
  if (m_remoteCancelButton) {
    connect(m_remoteCancelButton, &QPushButton::clicked, this,
            &RenderSettingsPanel::handleCancelRemoteJob);
  }

  connect(&m_queueModel, &RenderQueueModel::queueChanged, this,
          &RenderSettingsPanel::onQueueChanged);
  connect(&m_controller, &RenderController::queueProcessingChanged, this,
          &RenderSettingsPanel::onQueueProcessingChanged);
  connect(&m_controller, &RenderController::activeQueueJobChanged, this,
          &RenderSettingsPanel::onActiveQueueJobChanged);
  connect(&m_controller, &RenderController::queueDrained, this,
          &RenderSettingsPanel::onQueueDrained);
  connect(&m_controller, &RenderController::remoteWorkersChanged, this,
          &RenderSettingsPanel::populateRemoteWorkers);
}

void RenderSettingsPanel::syncFromModel() {
  const QSignalBlocker blockWidth(m_widthSpin);
  const QSignalBlocker blockHeight(m_heightSpin);
  const QSignalBlocker blockSamples(m_samplesSpin);

  m_widthSpin->setValue(m_model.width());
  m_heightSpin->setValue(m_model.height());
  m_samplesSpin->setValue(m_model.samples());
  const QString sceneText = m_model.scenePath().isEmpty()
                                ? QStringLiteral("objects.xml")
                                : m_model.scenePath();
  m_scenePathLabel->setText(sceneText);

  // Sync acceleration combo
  if (m_accelerationCombo) {
    const QSignalBlocker blockAccel(m_accelerationCombo);
    m_accelerationCombo->setCurrentIndex(m_model.useBVH() ? 1 : 0);
  }

  syncPresetUi();
  syncPostProcessingUi();
  syncRemoteSettingsUi();
}

void RenderSettingsPanel::syncPresetUi() {
  updatePresetStatusLabel();
  if (!m_presetList) {
    return;
  }

  const QString presetName = m_model.preset();
  const QSignalBlocker blocker(m_presetList);
  bool matched = false;
  for (int i = 0; i < m_presetList->count(); ++i) {
    QListWidgetItem *item = m_presetList->item(i);
    if (item->data(Qt::UserRole).toString() == presetName) {
      m_presetList->setCurrentItem(item);
      matched = true;
      break;
    }
  }

  if (!matched && m_presetList->count() > 0 &&
      presetName == QStringLiteral("Custom")) {
    m_presetList->setCurrentRow(0);
  }

  updatePresetControls();
}

void RenderSettingsPanel::syncPostProcessingUi() {
#if 0
    if (m_denoiserCheck) {
        const QSignalBlocker blocker(m_denoiserCheck);
        m_denoiserCheck->setChecked(m_model.denoiserEnabled());
    }

    if (m_toneMappingCombo) {
        const QSignalBlocker blocker(m_toneMappingCombo);
        const QString toneId = m_model.toneMapping();
        for (int i = 0; i < m_toneMappingCombo->count(); ++i) {
            if (m_toneMappingCombo->itemData(i).toString() == toneId) {
                m_toneMappingCombo->setCurrentIndex(i);
                break;
            }
        }
    }

    updateToneMappingDescription();
#endif
}

void RenderSettingsPanel::syncRemoteSettingsUi() {
  if (m_remoteDispatchCheck) {
    const QSignalBlocker blocker(m_remoteDispatchCheck);
    m_remoteDispatchCheck->setChecked(m_model.remoteRenderingEnabled());
  }

  if (m_remoteWorkerCombo) {
    const QSignalBlocker blocker(m_remoteWorkerCombo);
    const QString workerId = m_model.remoteWorkerId();
    int targetIndex = -1;
    for (int i = 0; i < m_remoteWorkerCombo->count(); ++i) {
      if (m_remoteWorkerCombo->itemData(i).toString().compare(
              workerId, Qt::CaseInsensitive) == 0) {
        targetIndex = i;
        break;
      }
    }
    if (targetIndex >= 0) {
      m_remoteWorkerCombo->setCurrentIndex(targetIndex);
    } else if (m_remoteWorkerCombo->count() > 0) {
      m_remoteWorkerCombo->setCurrentIndex(0);
      m_model.setRemoteWorkerId(m_remoteWorkerCombo->itemData(0).toString());
    }

    m_remoteWorkerCombo->setEnabled(m_model.remoteRenderingEnabled());
  }
}

void RenderSettingsPanel::populateRemoteWorkers() {
  if (!m_remoteWorkerCombo && !m_jobWorkerCombo) {
    return;
  }

  const QSignalBlocker blockerDefault(m_remoteWorkerCombo);
  const QSignalBlocker blockerJob(m_jobWorkerCombo);
  if (m_remoteWorkerCombo) {
    m_remoteWorkerCombo->clear();
  }
  if (m_jobWorkerCombo) {
    m_jobWorkerCombo->clear();
  }

  const auto &workers = m_controller.remoteWorkers();
  bool hasCredentialledWorkers = false;
  for (const auto &worker : workers) {
    const auto &definition = worker.definition;
    if (!definition.credentialKey.isEmpty()) {
      hasCredentialledWorkers = true;
    }
    QStringList descriptors;
    if (definition.transport == RemoteTransportMethod::SSH) {
      QString hostLabel = definition.host;
      if (definition.port > 0 && definition.port != 22) {
        hostLabel = tr("%1:%2").arg(definition.host).arg(definition.port);
      }
      descriptors << tr("SSH %1").arg(hostLabel);
      if (!definition.sshUser.isEmpty()) {
        descriptors << tr("user %1").arg(definition.sshUser);
      }
    } else {
      descriptors << tr("HTTP %1").arg(definition.baseUrl);
    }
    if (definition.maxConcurrency > 1) {
      descriptors << tr("%1 slots").arg(definition.maxConcurrency);
    }
    if (worker.activeJobs > 0) {
      descriptors << tr("%1 active").arg(worker.activeJobs);
    }
    QString label = definition.displayName;
    if (!descriptors.isEmpty()) {
      label = tr("%1 — %2").arg(definition.displayName,
                                descriptors.join(QLatin1String(" · ")));
    }
    if (!worker.available) {
      label = tr("%1 (offline)").arg(label);
    }
    if (m_remoteWorkerCombo) {
      m_remoteWorkerCombo->addItem(label, definition.id);
    }
    if (m_jobWorkerCombo) {
      m_jobWorkerCombo->addItem(label, definition.id);
    }
  }

  if (m_manageCredentialsButton) {
    m_manageCredentialsButton->setEnabled(hasCredentialledWorkers);
    if (!hasCredentialledWorkers) {
      m_manageCredentialsButton->setToolTip(
          tr("Remote workers need a 'credentialKey' to store tokens."));
    } else {
      m_manageCredentialsButton->setToolTip(QString());
    }
  }

  syncRemoteSettingsUi();
  syncSelectedJobDispatch();
  refreshWorkerStatus();
}

void RenderSettingsPanel::refreshWorkerStatus() {
  if (!m_workerStatusView) {
    return;
  }

  m_workerStatusView->clear();
  const auto &workers = m_controller.remoteWorkers();
  for (const auto &worker : workers) {
    auto *item = new QTreeWidgetItem(m_workerStatusView);
    item->setText(0, worker.definition.displayName);
    const QString transport =
        worker.definition.transport == RemoteTransportMethod::SSH ? tr("SSH")
                                                                  : tr("HTTP");
    item->setText(1, transport);
    QString endpoint;
    if (worker.definition.transport == RemoteTransportMethod::SSH) {
      if (worker.definition.sshUser.isEmpty()) {
        endpoint = worker.definition.host;
      } else {
        endpoint = QStringLiteral("%1@%2").arg(worker.definition.sshUser,
                                               worker.definition.host);
      }
      if (worker.definition.port > 0) {
        endpoint.append(QStringLiteral(":%1").arg(worker.definition.port));
      }
    } else {
      endpoint = worker.definition.baseUrl;
    }
    item->setText(2, endpoint);
    const QString status =
        worker.statusLabel.isEmpty()
            ? (worker.available ? tr("Ready") : tr("Offline"))
            : worker.statusLabel;
    item->setText(3, status);
  }
}

void RenderSettingsPanel::buildImportAssetsControls(QVBoxLayout *parentLayout) {
  auto *group = new QGroupBox(tr("Import Assets"), this);
  auto *layout = new QVBoxLayout(group);
  layout->setSpacing(8);

  m_importXmlButton = new QPushButton(tr("📄 XML Scene"), group);
  m_importXmlButton->setToolTip(
      tr("Import an existing scene XML file with all referenced meshes"));
  m_importXmlButton->setMinimumHeight(32);
  m_importXmlButton->setCursor(Qt::PointingHandCursor);
  m_importXmlButton->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #3a7bd5; color: white; border-radius: "
      "4px; padding: 6px 12px; font-weight: bold; text-align: left; }"
      "QPushButton:hover { background-color: #2a5ba5; }"
      "QPushButton:pressed { background-color: #1a4b95; }"));
  layout->addWidget(m_importXmlButton);

  m_importObjButton = new QPushButton(tr("🔷 OBJ Mesh"), group);
  m_importObjButton->setToolTip(
      tr("Import an OBJ mesh file and generate a default scene"));
  m_importObjButton->setMinimumHeight(32);
  m_importObjButton->setCursor(Qt::PointingHandCursor);
  m_importObjButton->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #00b894; color: white; border-radius: "
      "4px; padding: 6px 12px; font-weight: bold; text-align: left; }"
      "QPushButton:hover { background-color: #00a884; }"
      "QPushButton:pressed { background-color: #009874; }"));
  layout->addWidget(m_importObjButton);

  m_importMtlButton = new QPushButton(tr("🎨 MTL Material"), group);
  m_importMtlButton->setToolTip(tr("Import a material library file"));
  m_importMtlButton->setMinimumHeight(32);
  m_importMtlButton->setCursor(Qt::PointingHandCursor);
  m_importMtlButton->setStyleSheet(QStringLiteral(
      "QPushButton { background-color: #e17055; color: white; border-radius: "
      "4px; padding: 6px 12px; font-weight: bold; text-align: left; }"
      "QPushButton:hover { background-color: #d16045; }"
      "QPushButton:pressed { background-color: #c15035; }"));
  layout->addWidget(m_importMtlButton);

  group->setLayout(layout);
  parentLayout->addWidget(group);
}

void RenderSettingsPanel::buildPresetControls(QVBoxLayout *parentLayout) {
  auto *presetGroup = new QGroupBox(tr("Render Profiles"), this);
  auto *presetLayout = new QVBoxLayout(presetGroup);

  m_presetList = new QListWidget(presetGroup);
  m_presetList->setSelectionMode(QAbstractItemView::SingleSelection);
  presetLayout->addWidget(m_presetList);

  m_presetStatusLabel = new QLabel(presetGroup);
  presetLayout->addWidget(m_presetStatusLabel);

  // First row: Apply, Save, Delete
  auto *buttonRow1 = new QHBoxLayout();
  buttonRow1->setSpacing(4);
  m_presetApplyButton = new QPushButton(tr("Apply"), presetGroup);
  m_presetSaveButton = new QPushButton(tr("Save..."), presetGroup);
  m_presetDeleteButton = new QPushButton(tr("Delete"), presetGroup);
  buttonRow1->addWidget(m_presetApplyButton);
  buttonRow1->addWidget(m_presetSaveButton);
  buttonRow1->addWidget(m_presetDeleteButton);
  buttonRow1->addStretch();
  presetLayout->addLayout(buttonRow1);

  // Second row: Import, Export
  auto *buttonRow2 = new QHBoxLayout();
  buttonRow2->setSpacing(4);
  m_presetImportButton = new QPushButton(tr("Import..."), presetGroup);
  m_presetExportButton = new QPushButton(tr("Export..."), presetGroup);
  buttonRow2->addWidget(m_presetImportButton);
  buttonRow2->addWidget(m_presetExportButton);
  buttonRow2->addStretch();
  presetLayout->addLayout(buttonRow2);

  presetGroup->setLayout(presetLayout);
  parentLayout->addWidget(presetGroup);
}

void RenderSettingsPanel::buildPostProcessingControls(
    QVBoxLayout *parentLayout) {
#if 0
    auto *group = new QGroupBox(tr("Post-Processing"), this);
    auto *layout = new QVBoxLayout(group);

    m_denoiserCheck = new QCheckBox(tr("Enable adaptive denoiser"), group);
    layout->addWidget(m_denoiserCheck);

    auto *toneRow = new QHBoxLayout();
    toneRow->addWidget(new QLabel(tr("Tone mapping"), group));
    m_toneMappingCombo = new QComboBox(group);
    if (m_toneMappingCombo) {
        for (const auto &preset : tone::availableToneMappings()) {
            m_toneMappingCombo->addItem(preset.label, preset.id);
        }
    }
    toneRow->addWidget(m_toneMappingCombo, 1);
    layout->addLayout(toneRow);

    m_toneMappingDescription = new QLabel(group);
    m_toneMappingDescription->setWordWrap(true);
    layout->addWidget(m_toneMappingDescription);

    group->setLayout(layout);
    parentLayout->addWidget(group);
#endif
}

void RenderSettingsPanel::buildQueueControls(QVBoxLayout *parentLayout) {
#if 0
    auto *group = new QGroupBox(tr("Render Queue"), this);
    auto *layout = new QVBoxLayout(group);

    auto *buttonRow = new QHBoxLayout();
    m_queueAddButton = new QPushButton(tr("Add Current"), group);
    m_queueStartButton = new QPushButton(tr("Run Queue"), group);
    m_queueClearButton = new QPushButton(tr("Clear"), group);
    m_queueRemoveButton = new QPushButton(tr("Remove Selected"), group);
    m_queueRetryButton = new QPushButton(tr("Retry Selected"), group);

    buttonRow->addWidget(m_queueAddButton);
    buttonRow->addWidget(m_queueStartButton);
    buttonRow->addWidget(m_queueClearButton);
    buttonRow->addWidget(m_queueRemoveButton);
    buttonRow->addWidget(m_queueRetryButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    auto *outputRow = new QHBoxLayout();
    m_queueOutputEdit = new QLineEdit(group);
    m_queueOutputEdit->setPlaceholderText(tr("renders/<scene>_<preset>_<time>.png"));
    m_queueBrowseButton = new QPushButton(tr("Browse"), group);
    outputRow->addWidget(new QLabel(tr("Output Path"), group));
    outputRow->addWidget(m_queueOutputEdit, 1);
    outputRow->addWidget(m_queueBrowseButton);
    layout->addLayout(outputRow);

    m_remoteDispatchCheck = new QCheckBox(tr("Dispatch renders to a remote worker"), group);
    m_remoteDispatchCheck->setToolTip(tr("When enabled, queued jobs will be routed through a remote worker instead of using the local renderer."));
    layout->addWidget(m_remoteDispatchCheck);

    auto *remoteRow = new QHBoxLayout();
    remoteRow->addWidget(new QLabel(tr("Remote Worker"), group));
    m_remoteWorkerCombo = new QComboBox(group);
    remoteRow->addWidget(m_remoteWorkerCombo, 1);
    layout->addLayout(remoteRow);

    auto *credentialsRow = new QHBoxLayout();
    credentialsRow->addStretch();
    m_manageCredentialsButton = new QPushButton(tr("Manage Credentials..."), group);
    credentialsRow->addWidget(m_manageCredentialsButton);
    layout->addLayout(credentialsRow);

    auto *jobDispatchRow = new QHBoxLayout();
    jobDispatchRow->addWidget(new QLabel(tr("Job Dispatch"), group));
    m_jobDispatchCombo = new QComboBox(group);
    m_jobDispatchCombo->addItem(tr("Local Renderer"), static_cast<int>(RenderQueueModel::DispatchTarget::Local));
    m_jobDispatchCombo->addItem(tr("Remote Worker"), static_cast<int>(RenderQueueModel::DispatchTarget::Remote));
    m_jobWorkerCombo = new QComboBox(group);
    m_jobDispatchApplyButton = new QPushButton(tr("Apply"), group);
    jobDispatchRow->addWidget(m_jobDispatchCombo);
    jobDispatchRow->addWidget(m_jobWorkerCombo, 1);
    jobDispatchRow->addWidget(m_jobDispatchApplyButton);
    layout->addLayout(jobDispatchRow);

    m_queueList = new QListWidget(group);
    m_queueList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_queueList);

    m_queueStatusLabel = new QLabel(tr("Queue idle"), group);
    layout->addWidget(m_queueStatusLabel);

    auto *remoteStatusRow = new QHBoxLayout();
    m_remoteJobStatusLabel = new QLabel(tr("Remote jobs idle"), group);
    m_remoteCancelButton = new QPushButton(tr("Cancel Remote Job"), group);
    remoteStatusRow->addWidget(m_remoteJobStatusLabel, 1);
    remoteStatusRow->addWidget(m_remoteCancelButton);
    layout->addLayout(remoteStatusRow);

    group->setLayout(layout);
    parentLayout->addWidget(group);
#endif
}

void RenderSettingsPanel::buildWorkerStatusPanel(QVBoxLayout *parentLayout) {
#if 0
    auto *group = new QGroupBox(tr("Remote Worker Status"), this);
    auto *layout = new QVBoxLayout(group);

    m_workerStatusView = new QTreeWidget(group);
    m_workerStatusView->setColumnCount(4);
    m_workerStatusView->setHeaderLabels({tr("Worker"), tr("Transport"), tr("Endpoint"), tr("Status")});
    m_workerStatusView->setRootIsDecorated(false);
    m_workerStatusView->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_workerStatusView);

    group->setLayout(layout);
    parentLayout->addWidget(group);
#endif
}

void RenderSettingsPanel::handleAddToQueue() {
  const QString overridePath =
      m_queueOutputEdit ? m_queueOutputEdit->text().trimmed() : QString();
  if (!overridePath.isEmpty()) {
    m_controller.queueCurrentSettingsWithOutput(overridePath);
  } else {
    m_controller.queueCurrentSettings();
  }
}

void RenderSettingsPanel::handleBrowseOutput() {
  const QString current =
      m_queueOutputEdit ? m_queueOutputEdit->text().trimmed() : QString();
  const QString startDir = current.isEmpty()
                               ? QDir::currentPath()
                               : QFileInfo(current).absolutePath();
  const QString path =
      QFileDialog::getSaveFileName(this, tr("Select Output"), startDir,
                                   tr("Images (*.png *.ppm);;All Files (*)"));
  if (path.isEmpty() || !m_queueOutputEdit) {
    return;
  }
  m_queueOutputEdit->setText(path);
}

void RenderSettingsPanel::refreshPresetList() {
  if (!m_presetList) {
    return;
  }

  const QString activePreset = m_model.preset();
  const QString previousSelection = selectedPresetName();
  const QSignalBlocker blocker(m_presetList);

  m_presetList->clear();

  auto addItem = [this](const QString &display, const QString &name,
                        bool builtIn, bool deletable, const QString &tooltip) {
    auto *item = new QListWidgetItem(display, m_presetList);
    item->setData(Qt::UserRole, name);
    item->setData(Qt::UserRole + 1, builtIn);
    item->setData(Qt::UserRole + 2, deletable);
    item->setToolTip(tooltip);
  };

  addItem(tr("Custom (unsaved)"), QStringLiteral("Custom"), true, false,
          tr("Represents settings that do not match a saved profile."));

  for (const auto &preset : m_presetRepository.presets()) {
    QString label = preset.name;
    if (preset.builtIn) {
      label.append(tr(" (default)"));
    }

    QStringList tooltipParts;
    tooltipParts
        << tr("Resolution: %1 x %2").arg(preset.width).arg(preset.height);
    tooltipParts << tr("Samples: %1").arg(preset.samples);
    if (!preset.author.isEmpty()) {
      tooltipParts << tr("Author: %1").arg(preset.author);
    }
    if (!preset.description.isEmpty()) {
      tooltipParts << preset.description;
    }
    const QString tooltip = tooltipParts.join('\n');
    addItem(label, preset.name, preset.builtIn, !preset.builtIn, tooltip);
  }

  auto selectPreset = [this](const QString &name) {
    if (name.isEmpty() || !m_presetList) {
      return false;
    }
    for (int i = 0; i < m_presetList->count(); ++i) {
      auto *item = m_presetList->item(i);
      if (item->data(Qt::UserRole).toString() == name) {
        m_presetList->setCurrentItem(item);
        return true;
      }
    }
    return false;
  };

  if (!selectPreset(activePreset)) {
    selectPreset(previousSelection);
  }
  if (!m_presetList->currentItem() && m_presetList->count() > 0) {
    m_presetList->setCurrentRow(0);
  }

  updatePresetStatusLabel();
  updatePresetControls();
}

void RenderSettingsPanel::updatePresetStatusLabel() {
  if (!m_presetStatusLabel) {
    return;
  }

  const QString presetName = m_model.preset();
  if (presetName == QStringLiteral("Custom")) {
    m_presetStatusLabel->setText(tr("Active preset: Custom (unsaved)"));
    return;
  }

  const auto *preset = m_presetRepository.presetByName(presetName);
  if (!preset) {
    m_presetStatusLabel->setText(tr("Active preset: %1").arg(presetName));
    return;
  }

  const QString author =
      preset->author.isEmpty() ? tr("Unknown") : preset->author;
  m_presetStatusLabel->setText(tr("Active preset: %1 (%2 x %3 @ %4 spp, %5)")
                                   .arg(preset->name)
                                   .arg(preset->width)
                                   .arg(preset->height)
                                   .arg(preset->samples)
                                   .arg(author));
}

QString RenderSettingsPanel::selectedPresetName() const {
  if (!m_presetList) {
    return {};
  }
  QListWidgetItem *item = m_presetList->currentItem();
  if (!item) {
    return {};
  }
  return item->data(Qt::UserRole).toString();
}

bool RenderSettingsPanel::canDeleteSelectedPreset() const {
  const QString name = selectedPresetName();
  if (name.isEmpty() || name == QStringLiteral("Custom")) {
    return false;
  }

  const auto *preset = m_presetRepository.presetByName(name);
  return preset && !preset->builtIn;
}

void RenderSettingsPanel::updatePresetControls() {
  const bool hasSelection = !selectedPresetName().isEmpty();
  const bool canDelete = canDeleteSelectedPreset();
  const bool hasSharablePresets = !m_presetRepository.presets().isEmpty();

  if (m_presetApplyButton) {
    m_presetApplyButton->setEnabled(hasSelection);
  }
  if (m_presetDeleteButton) {
    m_presetDeleteButton->setEnabled(canDelete);
  }
  if (m_presetExportButton) {
    m_presetExportButton->setEnabled(hasSharablePresets);
  }
  if (m_presetSaveButton) {
    m_presetSaveButton->setEnabled(true);
  }
  if (m_presetImportButton) {
    m_presetImportButton->setEnabled(true);
  }

  updateToneMappingDescription();
}

void RenderSettingsPanel::handlePresetSelectionChanged() {
  updatePresetControls();
}

void RenderSettingsPanel::handlePresetActivated(QListWidgetItem *item) {
  if (!item) {
    return;
  }
  const QString name = item->data(Qt::UserRole).toString();
  if (name.isEmpty()) {
    return;
  }
  m_model.setPreset(name);
}

void RenderSettingsPanel::handleToneMappingChanged(int index) {
#if 0
    if (!m_toneMappingCombo || index < 0) {
        return;
    }
    const QString id = m_toneMappingCombo->itemData(index).toString();
    if (id.isEmpty()) {
        return;
    }
    m_model.setToneMapping(id);
    updateToneMappingDescription();
#endif
}

void RenderSettingsPanel::handleRemoteToggleChanged(bool enabled) {
  m_model.setRemoteRenderingEnabled(enabled);
  if (m_remoteWorkerCombo) {
    m_remoteWorkerCombo->setEnabled(enabled);
  }
}

void RenderSettingsPanel::handleRemoteWorkerChanged(int index) {
  if (!m_remoteWorkerCombo || index < 0) {
    return;
  }
  const QString workerId = m_remoteWorkerCombo->itemData(index).toString();
  if (workerId.isEmpty()) {
    return;
  }
  m_model.setRemoteWorkerId(workerId);
}

void RenderSettingsPanel::handleManageCredentials() {
  QVector<RemoteWorkerDefinition> securedWorkers;
  for (const auto &info : m_controller.remoteWorkers()) {
    if (!info.definition.credentialKey.isEmpty()) {
      securedWorkers.append(info.definition);
    }
  }

  if (securedWorkers.isEmpty()) {
    QMessageBox::information(
        this, tr("Remote Credentials"),
        tr("No workers declare a credential key. Add a 'credentialKey' entry "
           "in remote_workers.json to require tokens."));
    return;
  }

  RemoteCredentialDialog dialog(m_controller.credentialsStore(), securedWorkers,
                                this);
  dialog.exec();
}

void RenderSettingsPanel::updateToneMappingDescription() {
#if 0
    if (!m_toneMappingDescription) {
        return;
    }
    const QString toneId = m_model.toneMapping();
    QString label = tone::displayName(toneId);
    if (label.isEmpty()) {
        label = tr("Neutral");
    }
    QString desc = tone::descriptionFor(toneId);
    if (desc.isEmpty()) {
        desc = tr("Applies no additional tone mapping.");
    }
    m_toneMappingDescription->setText(tr("%1 — %2").arg(label, desc));
#endif
}

void RenderSettingsPanel::handleSavePreset() {
  PresetEditorDialog dialog(m_model.width(), m_model.height(),
                            m_model.samples(), this);
  if (m_model.preset() != QStringLiteral("Custom")) {
    dialog.setInitialName(m_model.preset());
  }
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  PresetRepository::Preset preset = dialog.preset();
  if (!m_presetRepository.addOrUpdatePreset(preset, false)) {
    QMessageBox::warning(this, tr("Save Preset"),
                         tr("A built-in preset already uses that name."));
    return;
  }
  m_model.setPreset(preset.name);
}

void RenderSettingsPanel::handleDeletePreset() {
  if (!canDeleteSelectedPreset()) {
    return;
  }
  const QString name = selectedPresetName();
  const auto reply = QMessageBox::question(
      this, tr("Delete Preset"),
      tr("Delete preset '%1'? This cannot be undone.").arg(name));
  if (reply != QMessageBox::Yes) {
    return;
  }
  m_presetRepository.removePreset(name);
}

void RenderSettingsPanel::handleImportPresets() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Import Preset Bundle"), QDir::currentPath(),
      tr("Preset Bundles (*.json);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  QStringList imported;
  QString error;
  if (!m_presetRepository.importFromFile(path, &imported, &error)) {
    QMessageBox::warning(this, tr("Import Failed"), error);
    return;
  }

  QMessageBox::information(this, tr("Presets Imported"),
                           tr("Imported %1 preset(s).").arg(imported.size()));
}

void RenderSettingsPanel::handleExportPresets() {
  QStringList selection;
  const QString selected = selectedPresetName();
  if (!selected.isEmpty() && selected != QStringLiteral("Custom")) {
    selection << selected;
  }

  const QString defaultPath =
      QDir::current().absoluteFilePath(QStringLiteral("render_presets.json"));
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export Preset Bundle"), defaultPath,
      tr("Preset Bundles (*.json);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  QString error;
  if (!m_presetRepository.exportToFile(path, selection, &error)) {
    QMessageBox::warning(this, tr("Export Failed"), error);
    return;
  }

  QMessageBox::information(this, tr("Presets Exported"),
                           tr("Preset bundle saved."));
}

void RenderSettingsPanel::handleStartQueue() {
  m_controller.startQueuedRenders();
}

void RenderSettingsPanel::handleClearQueue() {
  m_controller.clearQueuedRenders();
}

void RenderSettingsPanel::handleRemoveSelectedJob() {
  const QUuid id = selectedJobId();
  if (id.isNull()) {
    return;
  }
  m_queueModel.remove(id);
}

void RenderSettingsPanel::handleRetrySelectedJob() {
  if (m_queueProcessing) {
    return;
  }

  const QUuid id = selectedJobId();
  if (id.isNull()) {
    return;
  }

  const auto &jobs = m_queueModel.jobs();
  auto it = std::find_if(jobs.cbegin(), jobs.cend(),
                         [&id](const RenderQueueModel::QueuedRender &job) {
                           return job.id == id;
                         });
  if (it == jobs.cend()) {
    return;
  }

  RenderQueueModel::QueuedRender clone = *it;
  clone.id = {};
  clone.remoteStatus.clear();
  clone.remoteJobId.clear();
  clone.remoteArtifact.clear();
  m_queueModel.enqueue(clone);
}

void RenderSettingsPanel::handleApplyJobDispatch() {
  if (!m_jobDispatchCombo || m_queueProcessing) {
    return;
  }

  const QUuid id = selectedJobId();
  if (id.isNull()) {
    return;
  }

  const int targetValue = m_jobDispatchCombo->currentData().toInt();
  auto target = RenderQueueModel::DispatchTarget::Local;
  if (targetValue ==
      static_cast<int>(RenderQueueModel::DispatchTarget::Remote)) {
    target = RenderQueueModel::DispatchTarget::Remote;
  }
  QString workerId;
  if (target == RenderQueueModel::DispatchTarget::Remote && m_jobWorkerCombo) {
    workerId = m_jobWorkerCombo->currentData().toString();
  }

  if (!m_controller.updateQueuedJobDispatch(id, target, workerId)) {
    QMessageBox::warning(
        this, tr("Unable to Update Job"),
        tr("The selected job cannot be modified while it is rendering."));
    syncSelectedJobDispatch();
    return;
  }

  syncSelectedJobDispatch();
  updateQueueControls();
}

void RenderSettingsPanel::handleCancelRemoteJob() {
  if (!m_activeJobIsRemote || !m_controller.isRemoteJobRunning()) {
    return;
  }
  m_controller.stopRender();
}

void RenderSettingsPanel::onQueueChanged() {
  refreshQueueList();
  updateQueueControls();
}

void RenderSettingsPanel::onQueueProcessingChanged(bool running) {
  m_queueProcessing = running;
  if (!running) {
    m_activeJobId = {};
    m_activeJobIsRemote = false;
    updateRemoteQueueStatus(nullptr);
  }
  updateQueueControls();
}

void RenderSettingsPanel::onActiveQueueJobChanged(
    const RenderQueueModel::QueuedRender &job) {
  m_activeJobId = job.id;
  m_activeJobIsRemote = job.target == RenderQueueModel::DispatchTarget::Remote;
  if (m_queueStatusLabel) {
    m_queueStatusLabel->setText(
        tr("Rendering queued job: %1").arg(describeJob(job)));
  }
  updateRemoteQueueStatus(&job);
  refreshQueueList();
  updateQueueControls();
}

void RenderSettingsPanel::onQueueDrained() {
  m_activeJobId = {};
  m_activeJobIsRemote = false;
  if (m_queueStatusLabel) {
    m_queueStatusLabel->setText(tr("Queue finished"));
  }
  updateRemoteQueueStatus(nullptr);
  updateQueueControls();
}

void RenderSettingsPanel::updateRemoteQueueStatus(
    const RenderQueueModel::QueuedRender *job) {
  if (!m_remoteJobStatusLabel) {
    return;
  }

  if (!job) {
    m_remoteJobStatusLabel->setText(tr("Remote jobs idle"));
    return;
  }

  if (job->target != RenderQueueModel::DispatchTarget::Remote) {
    m_remoteJobStatusLabel->setText(tr("Local render in progress"));
    return;
  }

  const QString workerLabel = job->remoteWorkerLabel.isEmpty()
                                  ? tr("auto-select")
                                  : job->remoteWorkerLabel;
  const QString statusLabel =
      job->remoteStatus.isEmpty() ? tr("Dispatching") : job->remoteStatus;
  m_remoteJobStatusLabel->setText(tr("%1 — %2").arg(workerLabel, statusLabel));
}

void RenderSettingsPanel::syncSelectedJobDispatch() {
  if (!m_jobDispatchCombo) {
    return;
  }

  const QUuid id = selectedJobId();
  const QSignalBlocker blockDispatch(m_jobDispatchCombo);
  const QSignalBlocker blockWorker(m_jobWorkerCombo);

  if (id.isNull()) {
    m_jobDispatchCombo->setCurrentIndex(0);
    if (m_jobWorkerCombo) {
      m_jobWorkerCombo->setEnabled(false);
      if (m_jobWorkerCombo->count() > 0) {
        m_jobWorkerCombo->setCurrentIndex(0);
      }
    }
    if (m_jobDispatchApplyButton) {
      m_jobDispatchApplyButton->setEnabled(false);
    }
    return;
  }

  const auto &jobs = m_queueModel.jobs();
  auto it = std::find_if(jobs.cbegin(), jobs.cend(),
                         [&id](const RenderQueueModel::QueuedRender &job) {
                           return job.id == id;
                         });
  if (it == jobs.cend()) {
    return;
  }

  const int dispatchIndex =
      m_jobDispatchCombo->findData(static_cast<int>(it->target));
  if (dispatchIndex >= 0) {
    m_jobDispatchCombo->setCurrentIndex(dispatchIndex);
  }

  if (m_jobWorkerCombo) {
    const bool remote = it->target == RenderQueueModel::DispatchTarget::Remote;
    m_jobWorkerCombo->setEnabled(remote && !m_queueProcessing);
    if (remote && !it->remoteWorkerId.isEmpty()) {
      const int workerIndex = m_jobWorkerCombo->findData(it->remoteWorkerId);
      if (workerIndex >= 0) {
        m_jobWorkerCombo->setCurrentIndex(workerIndex);
      }
    }
  }

  if (m_jobDispatchApplyButton) {
    m_jobDispatchApplyButton->setEnabled(!m_queueProcessing);
  }
}

void RenderSettingsPanel::refreshQueueList() {
  if (!m_queueList) {
    return;
  }
  const QUuid previousSelection = selectedJobId();

  m_queueList->clear();
  for (const auto &job : m_queueModel.jobs()) {
    auto *item = new QListWidgetItem(describeJob(job), m_queueList);
    item->setData(Qt::UserRole, job.id);
    if (!m_activeJobId.isNull() && job.id == m_activeJobId) {
      QFont font = item->font();
      font.setBold(true);
      item->setFont(font);
      item->setText(tr("> %1").arg(item->text()));
    }
  }

  if (!previousSelection.isNull()) {
    for (int i = 0; i < m_queueList->count(); ++i) {
      auto *item = m_queueList->item(i);
      if (item->data(Qt::UserRole).toUuid() == previousSelection) {
        m_queueList->setCurrentItem(item);
        break;
      }
    }
  }

  if (m_queueStatusLabel && !m_queueProcessing && m_activeJobId.isNull()) {
    m_queueStatusLabel->setText(
        tr("Queue idle (%1 queued)").arg(m_queueModel.size()));
  }

  syncSelectedJobDispatch();
}

void RenderSettingsPanel::updateQueueControls() {
  const bool queueEmpty = m_queueModel.isEmpty();
  const bool hasSelection = !selectedJobId().isNull();
  const bool remoteChoice =
      m_jobDispatchCombo &&
      m_jobDispatchCombo->currentData().toInt() ==
          static_cast<int>(RenderQueueModel::DispatchTarget::Remote);

  if (m_queueAddButton) {
    m_queueAddButton->setEnabled(true);
  }
  if (m_queueStartButton) {
    m_queueStartButton->setEnabled(!queueEmpty && !m_queueProcessing);
  }
  if (m_queueClearButton) {
    m_queueClearButton->setEnabled(!queueEmpty && !m_queueProcessing);
  }
  if (m_queueRemoveButton) {
    m_queueRemoveButton->setEnabled(hasSelection && !m_queueProcessing);
  }
  if (m_queueRetryButton) {
    m_queueRetryButton->setEnabled(hasSelection && !m_queueProcessing);
  }
  if (m_jobDispatchCombo) {
    m_jobDispatchCombo->setEnabled(hasSelection && !m_queueProcessing);
  }
  if (m_jobWorkerCombo) {
    const bool enableWorker =
        hasSelection && !m_queueProcessing && remoteChoice;
    m_jobWorkerCombo->setEnabled(enableWorker);
  }
  if (m_jobDispatchApplyButton) {
    m_jobDispatchApplyButton->setEnabled(hasSelection && !m_queueProcessing);
  }
  if (m_remoteCancelButton) {
    const bool remoteRunning = m_activeJobIsRemote && m_queueProcessing &&
                               m_controller.isRemoteJobRunning();
    m_remoteCancelButton->setEnabled(remoteRunning);
  }
}

QString RenderSettingsPanel::describeJob(
    const RenderQueueModel::QueuedRender &job) const {
  const QFileInfo info(job.scenePath);
  const QString sceneName =
      info.fileName().isEmpty() ? job.scenePath : info.fileName();
  const QString preset =
      job.presetLabel.isEmpty() ? tr("Custom") : job.presetLabel;
  const QString output = job.outputPath.isEmpty()
                             ? tr("auto output")
                             : QFileInfo(job.outputPath).fileName();
  QStringList tags;
  if (job.denoiserEnabled) {
    tags << tr("denoise");
  }
  const QString toneLabel = tone::displayName(job.toneMapping);
  if (!toneLabel.isEmpty() &&
      job.toneMapping.compare(QStringLiteral("neutral"), Qt::CaseInsensitive) !=
          0) {
    tags << toneLabel;
  }
  if (job.target == RenderQueueModel::DispatchTarget::Remote) {
    const QString workerTag = job.remoteWorkerLabel.isEmpty()
                                  ? tr("remote (auto)")
                                  : tr("remote: %1").arg(job.remoteWorkerLabel);
    tags << workerTag;
  }

  const QString tagSuffix =
      tags.isEmpty() ? QString()
                     : tr(" [%1]").arg(tags.join(QLatin1String(", ")));

  return tr("%1 — %2 (%3x%4 @ %5 spp) → %6%7")
      .arg(sceneName)
      .arg(preset)
      .arg(job.width)
      .arg(job.height)
      .arg(job.samples)
      .arg(output)
      .arg(tagSuffix);
}

QUuid RenderSettingsPanel::selectedJobId() const {
  if (!m_queueList) {
    return {};
  }
  QListWidgetItem *item = m_queueList->currentItem();
  if (!item) {
    return {};
  }
  return item->data(Qt::UserRole).toUuid();
}

void RenderSettingsPanel::handleImportXml() {
  const QString startDir = m_model.scenePath().isEmpty()
                               ? QDir::currentPath()
                               : QFileInfo(m_model.scenePath()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Import Scene (XML)"), startDir,
                                   tr("Scene Files (*.xml);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  emit importXmlRequested(path);
}

void RenderSettingsPanel::handleImportObj() {
  const QString startDir = m_model.scenePath().isEmpty()
                               ? QDir::currentPath()
                               : QFileInfo(m_model.scenePath()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Import Mesh (OBJ)"), startDir,
                                   tr("Mesh Files (*.obj);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  emit importObjRequested(path);
}

void RenderSettingsPanel::handleImportMtl() {
  const QString startDir = m_model.scenePath().isEmpty()
                               ? QDir::currentPath()
                               : QFileInfo(m_model.scenePath()).absolutePath();
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Import Material (MTL)"), startDir,
                                   tr("Material Files (*.mtl);;All Files (*)"));
  if (path.isEmpty()) {
    return;
  }

  emit importMtlRequested(path);
}

void RenderSettingsPanel::handleAccelerationChanged(int index) {
  // index 0 = Linear, index 1 = BVH
  m_model.setUseBVH(index == 1);
}

} // namespace ui
} // namespace Raytracer
