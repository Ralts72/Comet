#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"

#include <filesystem>

namespace Comet {
    class COMET_API EnvironmentImporter final {
    public:
        [[nodiscard]] Result<TextureData> import(const std::filesystem::path& source_path) const;
    };
}
