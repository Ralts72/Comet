#pragma once

#include "asset/data/texture_data.h"
#include "asset/import_settings.h"
#include "common/export.h"
#include "common/result.h"

#include <cstddef>
#include <filesystem>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        static constexpr std::size_t MAX_WORKING_BYTES = 1024ull * 1024 * 1024;
        [[nodiscard]] static Result<std::size_t> working_bytes(
            const std::filesystem::path& source_path);
        [[nodiscard]] Result<TextureData> import(const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {},
            std::size_t memory_budget = MAX_WORKING_BYTES) const;
    };
}
