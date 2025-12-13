#include "ui/rendering/ToneMappingPresets.h"

#include <QObject>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Raytracer {
namespace ui {
namespace tone {

namespace {

using CurveFn = std::function<double(double)>;

std::array<uint8_t, 256> makeCurve(const CurveFn &fn)
{
    std::array<uint8_t, 256> curve {};
    for (int i = 0; i < 256; ++i) {
        const double x = static_cast<double>(i) / 255.0;
        double y = std::clamp(fn(x), 0.0, 1.0);
        curve[static_cast<size_t>(i)] = static_cast<uint8_t>(std::round(y * 255.0));
    }
    return curve;
}

ToneMappingPreset buildNeutral()
{
    ToneMappingPreset preset;
    preset.id = QStringLiteral("neutral");
    preset.label = QObject::tr("Neutral");
    preset.description = QObject::tr("Leaves the render untouched.");
    for (int i = 0; i < 256; ++i) {
        const auto component = static_cast<uint8_t>(i);
        preset.lutR[static_cast<size_t>(i)] = component;
        preset.lutG[static_cast<size_t>(i)] = component;
        preset.lutB[static_cast<size_t>(i)] = component;
    }
    return preset;
}

ToneMappingPreset buildFilmic()
{
    ToneMappingPreset preset;
    preset.id = QStringLiteral("filmic");
    preset.label = QObject::tr("Filmic Soft" );
    preset.description = QObject::tr("Soft shoulder curve that protects highlights.");
    auto curve = makeCurve([](double x) {
        const double a = 2.51;
        const double b = 0.03;
        const double c = 2.43;
        const double d = 0.59;
        const double e = 0.14;
        double y = (x * (a * x + b)) / (x * (c * x + d) + e);
        return std::clamp(y, 0.0, 1.0);
    });
    preset.lutR = curve;
    preset.lutG = curve;
    preset.lutB = curve;
    return preset;
}

ToneMappingPreset buildVibrantWarm()
{
    ToneMappingPreset preset;
    preset.id = QStringLiteral("vibrant_warm");
    preset.label = QObject::tr("Vibrant Warm");
    preset.description = QObject::tr("Adds gentle contrast with a warm bias for dusk shots.");
    preset.lutR = makeCurve([](double x) {
        double boosted = std::pow(x, 0.85);
        return std::clamp(boosted * 1.05, 0.0, 1.0);
    });
    preset.lutG = makeCurve([](double x) {
        double boosted = std::pow(x, 0.9);
        return std::clamp(boosted * 1.02, 0.0, 1.0);
    });
    preset.lutB = makeCurve([](double x) {
        double cooled = std::pow(x, 1.05) * 0.95 + 0.02;
        return std::clamp(cooled, 0.0, 1.0);
    });
    return preset;
}

ToneMappingPreset buildNocturne()
{
    ToneMappingPreset preset;
    preset.id = QStringLiteral("nocturne");
    preset.label = QObject::tr("Nocturne");
    preset.description = QObject::tr("Lifts shadows while keeping a cool cinematic tint.");
    preset.lutR = makeCurve([](double x) {
        double y = std::pow(x, 1.1) * 0.95 + 0.03;
        return std::clamp(y, 0.0, 1.0);
    });
    preset.lutG = makeCurve([](double x) {
        double y = std::pow(x, 1.0) * 0.92 + 0.05;
        return std::clamp(y, 0.0, 1.0);
    });
    preset.lutB = makeCurve([](double x) {
        double y = std::pow(x, 0.9) * 1.05 + 0.02;
        return std::clamp(y, 0.0, 1.0);
    });
    return preset;
}

} // namespace

const QVector<ToneMappingPreset> &availableToneMappings()
{
    static QVector<ToneMappingPreset> presets = [] {
        QVector<ToneMappingPreset> list;
        list.reserve(4);
        list.push_back(buildNeutral());
        list.push_back(buildFilmic());
        list.push_back(buildVibrantWarm());
        list.push_back(buildNocturne());
        return list;
    }();
    return presets;
}

const ToneMappingPreset *presetById(const QString &id)
{
    const auto &list = availableToneMappings();
    for (const auto &preset : list) {
        if (preset.id.compare(id, Qt::CaseInsensitive) == 0) {
            return &preset;
        }
    }
    return nullptr;
}

QStringList toneMappingLabels()
{
    QStringList labels;
    for (const auto &preset : availableToneMappings()) {
        labels << preset.label;
    }
    return labels;
}

QString displayName(const QString &id)
{
    const auto *preset = presetById(id);
    return preset ? preset->label : QString();
}

QString descriptionFor(const QString &id)
{
    const auto *preset = presetById(id);
    return preset ? preset->description : QString();
}

} // namespace tone
} // namespace ui
} // namespace Raytracer
