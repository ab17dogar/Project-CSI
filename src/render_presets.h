#pragma once

#include <array>
#include <string_view>

namespace Raytracer::presets {

struct RenderPresetDefinition {
    const char *name;
    int width;
    int height;
    int samples;
};

inline constexpr std::array<RenderPresetDefinition, 3> kRenderPresets = {{
    {"Preview", 640, 360, 5},
    {"Draft", 1280, 720, 25},
    {"Final", 1920, 1080, 200}
}};

inline const RenderPresetDefinition *findPreset(std::string_view name)
{
    for (const auto &preset : kRenderPresets) {
        if (name == preset.name) {
            return &preset;
        }
    }
    return nullptr;
}

inline std::string_view defaultPresetName()
{
    return kRenderPresets.back().name;
}

} // namespace Raytracer::presets
