#pragma once

#include "asset/metadata.h"

#include <cstdint>

namespace CometEditor {
    struct AssetDragPayload {
        static constexpr const char* TYPE = "COMET_PROJECT_ASSET";
        Comet::AssetHandle handle;
        Comet::AssetRevision revision;
        std::uint64_t generation;
        Comet::AssetType type;
    };
}
