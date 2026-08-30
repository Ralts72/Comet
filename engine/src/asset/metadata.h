#pragma once

#include "asset/handle.h"
#include "common/export.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace Comet {
    enum class AssetType : std::uint8_t {
        Unknown,
        Texture,
        Material,
        Mesh,
        Shader,
        Scene
    };

    [[nodiscard]] COMET_API std::string_view to_string(AssetType type) noexcept;
    [[nodiscard]] COMET_API std::optional<AssetType> asset_type_from_string(
        std::string_view value) noexcept;

    struct AssetMetadata {
        AssetHandle handle;
        AssetType type = AssetType::Unknown;

        auto operator<=>(const AssetMetadata&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::filesystem::path metadata_path(
        const std::filesystem::path& asset_path);
}
