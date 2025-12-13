#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace Raytracer {
namespace ui {

class PresetRepository;

} // namespace ui
} // namespace Raytracer

namespace Raytracer {
namespace ui {

class RenderSettingsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY settingsChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY settingsChanged)
    Q_PROPERTY(int samples READ samples WRITE setSamples NOTIFY settingsChanged)
    Q_PROPERTY(QString preset READ preset WRITE setPreset NOTIFY presetChanged)
    Q_PROPERTY(QString scenePath READ scenePath WRITE setScenePath NOTIFY scenePathChanged)
    Q_PROPERTY(bool denoiserEnabled READ denoiserEnabled WRITE setDenoiserEnabled NOTIFY postProcessingChanged)
    Q_PROPERTY(QString toneMapping READ toneMapping WRITE setToneMapping NOTIFY postProcessingChanged)
    Q_PROPERTY(bool remoteRenderingEnabled READ remoteRenderingEnabled WRITE setRemoteRenderingEnabled NOTIFY remoteSettingsChanged)
    Q_PROPERTY(QString remoteWorkerId READ remoteWorkerId WRITE setRemoteWorkerId NOTIFY remoteSettingsChanged)
    Q_PROPERTY(bool useBVH READ useBVH WRITE setUseBVH NOTIFY settingsChanged)

public:
    explicit RenderSettingsModel(PresetRepository &repository, QObject *parent = nullptr);

    int width() const noexcept { return m_width; }
    int height() const noexcept { return m_height; }
    int samples() const noexcept { return m_samples; }
    QString scenePath() const { return m_scenePath; }
    QString preset() const { return m_preset; }
    QStringList availablePresets() const;
    bool denoiserEnabled() const noexcept { return m_denoiserEnabled; }
    QString toneMapping() const { return m_toneMapping; }
    bool remoteRenderingEnabled() const noexcept { return m_remoteRenderingEnabled; }
    QString remoteWorkerId() const { return m_remoteWorkerId; }
    QStringList availableToneMappings() const;
    PresetRepository &presetRepository() const { return m_presetRepository; }
    bool useBVH() const noexcept { return m_useBVH; }

public slots:
    void setWidth(int value);
    void setHeight(int value);
    void setSamples(int value);
    void setScenePath(const QString &path);
    void setPreset(const QString &presetName);
    void setDenoiserEnabled(bool enabled);
    void setToneMapping(const QString &toneMappingId);
    void setRemoteRenderingEnabled(bool enabled);
    void setRemoteWorkerId(const QString &workerId);
    void setUseBVH(bool enabled);

signals:
    void settingsChanged();
    void scenePathChanged(const QString &path);
    void presetChanged(const QString &preset);
    void postProcessingChanged();
    void remoteSettingsChanged();

private:
    void ensurePresetConsistency();
    void updatePresetLabel(const QString &name);

    PresetRepository &m_presetRepository;
    int m_width {1920};
    int m_height {1080};
    int m_samples {100};
    QString m_scenePath {QStringLiteral("objects.xml")};
    QString m_preset {QStringLiteral("Custom")};
    bool m_updatingFromPreset {false};
    bool m_denoiserEnabled {true};
    QString m_toneMapping {QStringLiteral("neutral")};
    bool m_remoteRenderingEnabled {false};
    QString m_remoteWorkerId {QStringLiteral("auto")};
    bool m_useBVH {false};  // Use BVH acceleration (default: linear)
};

} // namespace ui
} // namespace Raytracer
