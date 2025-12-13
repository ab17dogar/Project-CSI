#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include <array>
#include <cstdint>

namespace Raytracer {
namespace ui {
namespace tone {

struct ToneMappingPreset {
    QString id;
    QString label;
    QString description;
    std::array<uint8_t, 256> lutR;
    std::array<uint8_t, 256> lutG;
    std::array<uint8_t, 256> lutB;
};

const QVector<ToneMappingPreset> &availableToneMappings();
const ToneMappingPreset *presetById(const QString &id);
QStringList toneMappingLabels();
QString displayName(const QString &id);
QString descriptionFor(const QString &id);

} // namespace tone
} // namespace ui
} // namespace Raytracer
