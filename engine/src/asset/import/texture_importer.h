#pragma once

#include "asset/import_settings.h"
#include "common/export.h"
#include "common/result.h"
#include "asset/data/texture_data.h"

#include <filesystem>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        [[nodiscard]] Result<TextureData> import(const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {}) const;
    };
}
