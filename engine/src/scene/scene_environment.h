#pragma once

#include "asset/handle.h"

namespace Comet {
    struct SceneEnvironment {
        AssetHandle asset;
        bool background = false;
        float intensity = 1.0f;
        float rotation = 0.0f;
        bool lighting = false;
        float lighting_intensity = 1.0f;

        bool operator==(const SceneEnvironment&) const = default;
    };
}
