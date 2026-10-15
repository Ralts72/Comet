#pragma once

#include "asset/handle.h"

namespace Comet {
    // 各级共享材质、局部坐标与原点；阈值为物体占视图高度的比例。
    struct MeshLod {
        enum class Preview { Auto = -1, Original, Level1, Level2 };
        AssetHandle level1;
        AssetHandle level2;
        float level1_height = 0.25f;
        float level2_height = 0.08f;
        float hysteresis = 0.1f;
        Preview preview = Preview::Auto;
        bool operator==(const MeshLod&) const = default;
    };
}
