#include "ui/models/PresetRepository.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include "render_presets.h"

namespace Raytracer {
namespace ui {

namespace {
constexpr const char *kSettingsGroup = "presets";
constexpr const char *kSettingsArray = "custom";
}

PresetRepository::PresetRepository(QObject *parent)
    : QObject(parent)
{
    loadBuiltInPresets();
    loadCustomPresets();
    rebuildIndex();
}

QStringList PresetRepository::presetNames() const
{
    QStringList names;
    names.reserve(m_presets.size());
    for (const auto &preset : m_presets) {
        names << preset.name;
    }
    return names;
}

const PresetRepository::Preset *PresetRepository::presetByName(const QString &name) const
{
    const QString key = normalizeName(name);
    const auto it = m_index.constFind(key);
    if (it == m_index.cend()) {
        return nullptr;
    }
    const int idx = it.value();
    if (idx < 0 || idx >= m_presets.size()) {
        return nullptr;
    }
    return &m_presets.at(idx);
}

bool PresetRepository::addOrUpdatePreset(Preset preset, bool allowBuiltInOverride)
{
    preset.name = preset.name.trimmed();
    if (preset.name.isEmpty()) {
        return false;
    }

    const QString key = normalizeName(preset.name);
    const auto it = m_index.constFind(key);
    if (it != m_index.cend()) {
        const int idx = it.value();
        if (idx >= 0 && idx < m_presets.size()) {
            Preset &existing = m_presets[idx];
            if (existing.builtIn && !allowBuiltInOverride) {
                return false;
            }
            preset.builtIn = existing.builtIn;
            m_presets[idx] = preset;
            emit presetsChanged();
            saveCustomPresets();
            return true;
        }
    }

    preset.builtIn = false;
    m_presets.push_back(preset);
    rebuildIndex();
    emit presetsChanged();
    saveCustomPresets();
    return true;
}

bool PresetRepository::removePreset(const QString &name)
{
    const QString key = normalizeName(name);
    const auto it = m_index.constFind(key);
    if (it == m_index.cend()) {
        return false;
    }
    const int idx = it.value();
    if (idx < 0 || idx >= m_presets.size()) {
        return false;
    }
    if (m_presets[idx].builtIn) {
        return false;
    }
    m_presets.removeAt(idx);
    rebuildIndex();
    emit presetsChanged();
    saveCustomPresets();
    return true;
}

bool PresetRepository::importFromFile(const QString &path, QStringList *importedNames, QString *errorMessage)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to open %1").arg(path);
        }
        return false;
    }

    const QByteArray data = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        if (errorMessage) {
            *errorMessage = tr("Invalid preset bundle (expected JSON object)");
        }
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonArray presetsArray = root.value(QStringLiteral("presets")).toArray();
    if (presetsArray.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Preset bundle contains no presets");
        }
        return false;
    }

    QStringList added;
    for (const QJsonValue &value : presetsArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        Preset preset;
        preset.name = obj.value(QStringLiteral("name")).toString();
        preset.width = obj.value(QStringLiteral("width")).toInt(1920);
        preset.height = obj.value(QStringLiteral("height")).toInt(1080);
        preset.samples = obj.value(QStringLiteral("samples")).toInt(100);
        preset.author = obj.value(QStringLiteral("author")).toString();
        preset.description = obj.value(QStringLiteral("description")).toString();
        preset.builtIn = false;
        if (preset.name.trimmed().isEmpty()) {
            continue;
        }
        if (addOrUpdatePreset(preset, false)) {
            added << preset.name;
        }
    }

    if (added.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No valid presets were imported");
        }
        return false;
    }

    if (importedNames) {
        *importedNames = added;
    }
    return true;
}

bool PresetRepository::exportToFile(const QString &path, const QStringList &selectedNames, QString *errorMessage) const
{
    QVector<Preset> toExport;
    if (selectedNames.isEmpty()) {
        toExport = m_presets;
    } else {
        for (const QString &name : selectedNames) {
            if (const Preset *preset = presetByName(name)) {
                toExport.push_back(*preset);
            }
        }
    }

    if (toExport.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("No presets selected for export");
        }
        return false;
    }

    QJsonArray presetsJson;
    for (const Preset &preset : toExport) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), preset.name);
        obj.insert(QStringLiteral("width"), preset.width);
        obj.insert(QStringLiteral("height"), preset.height);
        obj.insert(QStringLiteral("samples"), preset.samples);
        if (!preset.author.isEmpty()) {
            obj.insert(QStringLiteral("author"), preset.author);
        }
        if (!preset.description.isEmpty()) {
            obj.insert(QStringLiteral("description"), preset.description);
        }
        presetsJson.append(obj);
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("presets"), presetsJson);

    const QJsonDocument doc(root);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage) {
            *errorMessage = tr("Unable to write %1").arg(path);
        }
        return false;
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

void PresetRepository::saveCustomPresets() const
{
    QSettings settings(QStringLiteral("Project-CSI"), QStringLiteral("Raytracer"));
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.remove(QString());
    settings.beginWriteArray(QString::fromLatin1(kSettingsArray));
    int index = 0;
    for (const Preset &preset : m_presets) {
        if (preset.builtIn) {
            continue;
        }
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("name"), preset.name);
        settings.setValue(QStringLiteral("width"), preset.width);
        settings.setValue(QStringLiteral("height"), preset.height);
        settings.setValue(QStringLiteral("samples"), preset.samples);
        settings.setValue(QStringLiteral("author"), preset.author);
        settings.setValue(QStringLiteral("description"), preset.description);
    }
    settings.endArray();
    settings.endGroup();
}

void PresetRepository::loadBuiltInPresets()
{
    for (const auto &definition : Raytracer::presets::kRenderPresets) {
        Preset preset;
        preset.name = QString::fromLatin1(definition.name);
        preset.width = definition.width;
        preset.height = definition.height;
        preset.samples = definition.samples;
        preset.author = QStringLiteral("Built-in");
        preset.description = QStringLiteral("Default profile");
        preset.builtIn = true;
        m_presets.push_back(preset);
    }
}

void PresetRepository::loadCustomPresets()
{
    QSettings settings(QStringLiteral("Project-CSI"), QStringLiteral("Raytracer"));
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const int count = settings.beginReadArray(QString::fromLatin1(kSettingsArray));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Preset preset;
        preset.name = settings.value(QStringLiteral("name")).toString();
        preset.width = settings.value(QStringLiteral("width"), 1920).toInt();
        preset.height = settings.value(QStringLiteral("height"), 1080).toInt();
        preset.samples = settings.value(QStringLiteral("samples"), 100).toInt();
        preset.author = settings.value(QStringLiteral("author")).toString();
        preset.description = settings.value(QStringLiteral("description")).toString();
        preset.builtIn = false;
        if (!preset.name.trimmed().isEmpty()) {
            m_presets.push_back(preset);
        }
    }
    settings.endArray();
    settings.endGroup();
}

void PresetRepository::rebuildIndex()
{
    m_index.clear();
    for (int i = 0; i < m_presets.size(); ++i) {
        m_index.insert(normalizeName(m_presets[i].name), i);
    }
}

QString PresetRepository::normalizeName(const QString &name) const
{
    QString lower = name.trimmed().toLower();
    return lower;
}

} // namespace ui
} // namespace Raytracer
