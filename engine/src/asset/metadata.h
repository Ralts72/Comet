#pragma once

#include "asset/handle.h"
#include "asset/import_settings.h"
#include "common/export.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    enum class AssetType : std::uint8_t {
        Unknown,
        Texture,
        Material,
        Mesh,
        Shader,
        ShaderProgram,
        Scene,
        Environment,
        Script,
        Audio,
        Model,
        Prefab,
        AnimationController,
        Data
    };

    [[nodiscard]] COMET_API std::string_view to_string(AssetType type) noexcept;
    [[nodiscard]] COMET_API std::optional<AssetType> asset_type_from_string(
        std::string_view value) noexcept;

    [[nodiscard]] COMET_API std::string source_extension(const std::filesystem::path& path);
    [[nodiscard]] COMET_API std::optional<AssetType> asset_type_from_path(
        const std::filesystem::path& path);
    // 依赖源码不生成独立资产身份或 .meta。
    [[nodiscard]] COMET_API bool is_source_only_path(const std::filesystem::path& path);
    [[nodiscard]] COMET_API bool is_lua_module_source(const std::filesystem::path& path);

    struct AssetSubresource {
        std::string key;
        AssetHandle handle;
        AssetType type = AssetType::Unknown;

        auto operator<=>(const AssetSubresource&) const noexcept = default;
    };

    struct AssetMetadata {
        AssetHandle handle;
        AssetType type = AssetType::Unknown;
        AssetImportSettings import_settings;
        // 模型子资源与源文件共用一份 .meta；key 标识源内容，handle 供场景持久引用。
        std::vector<AssetSubresource> subresources;

        auto operator<=>(const AssetMetadata&) const noexcept = default;
    };

    [[nodiscard]] COMET_API AssetImportSettings make_default_import_settings(AssetType type);

    [[nodiscard]] COMET_API std::filesystem::path metadata_path(
        const std::filesystem::path& asset_path);
}
