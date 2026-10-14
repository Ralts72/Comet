#pragma once

#include "asset/data/texture_data.h"
#include "asset/handle.h"
#include "asset/import/input_snapshot.h"
#include "asset/import_settings.h"

#include <optional>

namespace Comet {
    class COMET_API TextureArtifact final {
    public:
        [[nodiscard]] static std::optional<TextureArtifact> load(
            const std::filesystem::path& path, AssetHandle handle, std::size_t memory_budget);
        [[nodiscard]] Result<void> publish_atomic(const std::filesystem::path& path) const;

        AssetHandle handle;
        uint32_t importer_version = 0;
        ImportInputFingerprint source;
        TextureImportSettings settings;
        TextureData data;
    };
}
