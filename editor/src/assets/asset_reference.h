#pragma once

#include "asset/metadata.h"

#include <cstdint>
#include <optional>

struct ImGuiPayload;
namespace Comet {
    class AssetDatabase;
}

namespace CometEditor {
    struct AssetDragPayload {
        static constexpr const char* TYPE = "COMET_PROJECT_ASSET";
        Comet::AssetHandle handle;
        Comet::AssetRevision revision;
        std::uint64_t generation;
        Comet::AssetType type;
    };
    [[nodiscard]] bool edit_asset_reference(const char* label, Comet::AssetHandle& handle,
        const Comet::AssetDatabase& database, std::optional<Comet::AssetType> type,
        bool allow_none = true);

    [[nodiscard]] std::optional<AssetDragPayload> read_asset_drag_payload(
        const ImGuiPayload* payload);
}
