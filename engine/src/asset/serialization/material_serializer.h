#pragma once

#include "asset/data/material_data.h"
#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API MaterialSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 2;

        [[nodiscard]] Result<std::string> serialize(const MaterialData& data) const;
        [[nodiscard]] Result<MaterialData> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;

        [[nodiscard]] Result<void> save(
            const MaterialData& data, const std::filesystem::path& path) const;
        [[nodiscard]] Result<MaterialData> load(const std::filesystem::path& source_path) const;
    };
}
