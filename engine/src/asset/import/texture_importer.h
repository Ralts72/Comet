#pragma once

#include "asset/import_settings.h"
#include "common/export.h"
#include "render/resource/texture_data.h"

#include <filesystem>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        [[nodiscard]] TextureData import(
            const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {}) const;
    };
}
