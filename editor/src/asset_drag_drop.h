#pragma once

#include "asset/handle.h"
#include "asset/metadata.h"

#include <cstdint>

namespace CometEditor {
    // 拖拽跨帧保存身份，不持有 Project 列表或 Scene 内对象的地址。
    struct AssetDragPayload {
        static constexpr const char* TYPE = "COMET_ASSET";
        Comet::AssetHandle handle;
        std::uint64_t generation;
        Comet::AssetType type = Comet::AssetType::Unknown;
    };
}
