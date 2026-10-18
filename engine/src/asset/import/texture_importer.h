#pragma once

#include "asset/data/texture_data.h"
#include "asset/import_settings.h"
#include "asset/import/asset_task_types.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <filesystem>
#include <span>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        static constexpr uint32_t VERSION = 3;
        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source_path, const AssetImportLimits& limits = {});
        [[nodiscard]] static Result<std::size_t> working_bytes(
            std::span<const std::byte> encoded, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<TextureData> import(const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {},
            std::size_t memory_budget = AssetImportLimits{}.texture_working_bytes,
            const AssetImportLimits& limits = {}) const;
        [[nodiscard]] Result<TextureData> import(std::span<const std::byte> encoded,
            const TextureImportSettings& settings = {},
            std::size_t memory_budget = AssetImportLimits{}.texture_working_bytes,
            const AssetImportLimits& limits = {}) const;
    };
}
