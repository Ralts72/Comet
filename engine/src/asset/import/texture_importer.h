#pragma once

#include "common/export.h"
#include "render/texture.h"

#include <filesystem>

namespace Comet {
    class COMET_API TextureImporter final {
    public:
        [[nodiscard]] TextureData import(const std::filesystem::path& source_path) const;
    };
}
