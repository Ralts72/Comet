#pragma once

#include "asset/data/environment_data.h"
#include "asset/handle.h"
#include "asset/import/input_snapshot.h"

#include <optional>

namespace Comet {
    class COMET_API EnvironmentArtifact final {
    public:
        [[nodiscard]] static std::optional<EnvironmentArtifact> load(
            const std::filesystem::path& path, AssetHandle handle, std::size_t memory_budget);
        [[nodiscard]] Result<void> publish_atomic(const std::filesystem::path& path) const;

        AssetHandle handle;
        uint32_t importer_version = 0;
        ImportInputFingerprint source;
        EnvironmentData data;
    };
}
