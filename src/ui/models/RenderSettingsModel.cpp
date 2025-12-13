#include "ui/models/RenderSettingsModel.h"

#include "ui/models/PresetRepository.h"
#include "ui/rendering/ToneMappingPresets.h"

#include <algorithm>

namespace Raytracer {
namespace ui {

namespace {
constexpr int kMinDimension = 16;
constexpr int kMaxDimension = 16384;
constexpr int kMinSamples = 1;
constexpr int kMaxSamples = 4096;
} // namespace

RenderSettingsModel::RenderSettingsModel(PresetRepository &repository, QObject *parent)
    : QObject(parent)
    , m_presetRepository(repository)
{
    connect(&m_presetRepository, &PresetRepository::presetsChanged, this, [this]() {
        ensurePresetConsistency();
    });
}

void RenderSettingsModel::setWidth(int value)
{
    const int clamped = std::clamp(value, kMinDimension, kMaxDimension);
    if (clamped == m_width) {
        return;
    }

    m_width = clamped;
    ensurePresetConsistency();
    emit settingsChanged();
}

void RenderSettingsModel::setHeight(int value)
{
    const int clamped = std::clamp(value, kMinDimension, kMaxDimension);
    if (clamped == m_height) {
        return;
    }

    m_height = clamped;
    ensurePresetConsistency();
    emit settingsChanged();
}

void RenderSettingsModel::setSamples(int value)
{
    const int clamped = std::clamp(value, kMinSamples, kMaxSamples);
    if (clamped == m_samples) {
        return;
    }

    m_samples = clamped;
    ensurePresetConsistency();
    emit settingsChanged();
}

void RenderSettingsModel::setScenePath(const QString &path)
{
    if (path == m_scenePath || path.isEmpty()) {
        return;
    }
    m_scenePath = path;
    emit scenePathChanged(m_scenePath);
}

void RenderSettingsModel::setPreset(const QString &presetName)
{
    if (presetName == m_preset) {
        return;
    }

    if (presetName == QStringLiteral("Custom")) {
        updatePresetLabel(QStringLiteral("Custom"));
        return;
    }

    const auto *preset = m_presetRepository.presetByName(presetName);
    if (!preset) {
        return;
    }

    m_updatingFromPreset = true;
    setWidth(preset->width);
    setHeight(preset->height);
    setSamples(preset->samples);
    m_updatingFromPreset = false;

    updatePresetLabel(presetName);
}

void RenderSettingsModel::setDenoiserEnabled(bool enabled)
{
    if (m_denoiserEnabled == enabled) {
        return;
    }
    m_denoiserEnabled = enabled;
    emit settingsChanged();
    emit postProcessingChanged();
}

void RenderSettingsModel::setToneMapping(const QString &toneMappingId)
{
    QString normalized = toneMappingId.trimmed();
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("neutral");
    }

    const auto *preset = tone::presetById(normalized);
    if (!preset) {
        return;
    }

    if (preset->id == m_toneMapping) {
        return;
    }

    m_toneMapping = preset->id;
    emit settingsChanged();
    emit postProcessingChanged();
}

void RenderSettingsModel::setRemoteRenderingEnabled(bool enabled)
{
    if (m_remoteRenderingEnabled == enabled) {
        return;
    }
    m_remoteRenderingEnabled = enabled;
    emit remoteSettingsChanged();
}

void RenderSettingsModel::setRemoteWorkerId(const QString &workerId)
{
    QString normalized = workerId.trimmed();
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("auto");
    }
    if (m_remoteWorkerId == normalized) {
        return;
    }
    m_remoteWorkerId = normalized;
    emit remoteSettingsChanged();
}

void RenderSettingsModel::setUseBVH(bool enabled)
{
    if (m_useBVH == enabled) {
        return;
    }
    m_useBVH = enabled;
    emit settingsChanged();
}

QStringList RenderSettingsModel::availablePresets() const
{
    QStringList names = m_presetRepository.presetNames();
    names.prepend(QStringLiteral("Custom"));
    return names;
}

QStringList RenderSettingsModel::availableToneMappings() const
{
    QStringList options;
    for (const auto &preset : tone::availableToneMappings()) {
        options << preset.label;
    }
    return options;
}

void RenderSettingsModel::ensurePresetConsistency()
{
    if (m_updatingFromPreset) {
        return;
    }

    const auto *preset = m_presetRepository.presetByName(m_preset);
    if (!preset) {
        if (m_preset != QStringLiteral("Custom")) {
            updatePresetLabel(QStringLiteral("Custom"));
        }
        return;
    }

    if (preset->width == m_width && preset->height == m_height && preset->samples == m_samples) {
        return;
    }

    updatePresetLabel(QStringLiteral("Custom"));
}

void RenderSettingsModel::updatePresetLabel(const QString &name)
{
    if (m_preset == name) {
        return;
    }
    m_preset = name;
    emit presetChanged(m_preset);
}

} // namespace ui
} // namespace Raytracer
