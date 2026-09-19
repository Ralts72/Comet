#pragma once

#include "asset/data/environment_data.h"
#include "common/export.h"
#include "common/result.h"

#include <filesystem>

namespace Comet {
    class COMET_API EnvironmentImporter final {
    public:
        static constexpr uint32_t VERSION = 2;
        static constexpr std::size_t MAX_WORKING_BYTES = 2ull * 1024 * 1024 * 1024;
        [[nodiscard]] static Result<std::size_t> working_bytes(const std::filesystem::path& path);
        [[nodiscard]] Result<void> validate_source(const std::filesystem::path& source_path) const;
        [[nodiscard]] Result<EnvironmentData> import(const std::filesystem::path& source_path,
            std::size_t memory_budget = MAX_WORKING_BYTES) const;

    private:
        static EnvironmentData prepare_lighting(TextureData background);
    };
}
