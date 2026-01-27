#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <vector>

#include "util/vec3.h"

class ray;
class world;

namespace render {

struct TileProgressStats {
    size_t tilesDone {0};
    size_t totalTiles {0};
    double avgTileMs {0.0};
    double estRemainingMs {0.0};
};

using TileCallback = std::function<void(const std::vector<color> &bitmap,
                                        int width,
                                        int height,
                                        const TileProgressStats &stats)>;

color TraceRay(const ray &r, int depth, world &sceneWorld);

void RenderSceneToBitmap(world &sceneWorld,
                         std::vector<color> &bitmap,
                         unsigned int threads,
                         int tile_size,
                         bool tile_debug,
                         const TileCallback &onTileFinished = TileCallback(),
                         std::atomic<bool> *cancelFlag = nullptr);

} // namespace render
