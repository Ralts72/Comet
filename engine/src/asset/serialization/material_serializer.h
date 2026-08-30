#pragma once

#include "asset/material_data.h"
#include "common/export.h"

#include <cstdint>
#include <filesystem>

namespace Comet {
    class COMET_API MaterialSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] MaterialData load(const std::filesystem::path& source_path) const;
    };
}
