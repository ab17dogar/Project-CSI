#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QHash>

namespace Raytracer {
namespace ui {

class PresetRepository : public QObject
{
    Q_OBJECT

public:
    struct Preset {
        QString name;
        int width {1920};
        int height {1080};
        int samples {100};
        QString author;
        QString description;
        bool builtIn {false};
    };

    explicit PresetRepository(QObject *parent = nullptr);

    QStringList presetNames() const;
    const QVector<Preset> &presets() const { return m_presets; }
    const Preset *presetByName(const QString &name) const;

    bool addOrUpdatePreset(Preset preset, bool allowBuiltInOverride = false);
    bool removePreset(const QString &name);

    bool importFromFile(const QString &path, QStringList *importedNames = nullptr, QString *errorMessage = nullptr);
    bool exportToFile(const QString &path, const QStringList &selectedNames = {}, QString *errorMessage = nullptr) const;

    void saveCustomPresets() const;

signals:
    void presetsChanged();

private:
    void loadBuiltInPresets();
    void loadCustomPresets();
    void rebuildIndex();
    QString normalizeName(const QString &name) const;

private:
    QVector<Preset> m_presets;
    QHash<QString, int> m_index; // normalized name -> index
};

} // namespace ui
} // namespace Raytracer
