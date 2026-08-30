#pragma once

#include "asset/import_settings.h"
#include "asset/import/input_snapshot.h"
#include "common/export.h"
#include "render/resource/texture_data.h"

#include <filesystem>

namespace Comet {
    struct COMET_API TextureImportResult {
        TextureData data;
        ImportInputSnapshot input_snapshot;
        bool inputs_changed_during_import = false;
    };

    class COMET_API TextureImporter final {
    public:
        [[nodiscard]] TextureData import(
            const std::filesystem::path& source_path,
            const TextureImportSettings& settings = {}) const;
        [[nodiscard]] TextureImportResult import_with_snapshot(
            const std::filesystem::path& source_path,
            const std::filesystem::path& asset_root,
            const TextureImportSettings& settings = {}) const;
    };
}
