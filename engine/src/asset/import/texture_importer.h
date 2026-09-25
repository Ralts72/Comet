#pragma once

#include "asset/data/texture_data.h"
#include "asset/import_settings.h"
#include "asset/import/asset_task_types.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <filesystem>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source_path, const AssetImportLimits& limits = {});
        [[nodiscard]] Result<TextureData> import(const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {},
            std::size_t memory_budget = AssetImportLimits{}.texture_working_bytes,
            const AssetImportLimits& limits = {}) const;
    };
}
