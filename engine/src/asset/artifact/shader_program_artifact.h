#pragma once

#include "asset/handle.h"
#include "asset/import/input_snapshot.h"
#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    // CPU-only compiled program. A published instance is never mutated by a consumer.
    class COMET_API ShaderProgramArtifact final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 2;

        [[nodiscard]] static std::optional<ShaderProgramArtifact> load(
            const std::filesystem::path& path, AssetHandle handle);
        [[nodiscard]] Result<void> publish_atomic(const std::filesystem::path& path) const;

        AssetHandle handle;
        ImportInputSnapshot inputs;
        std::vector<std::uint32_t> vertex_words;
        std::vector<std::uint32_t> fragment_words;
        std::string vertex_entry = "main";
        std::string fragment_entry = "main";
    };
}
