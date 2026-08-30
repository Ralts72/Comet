#pragma once

#include "asset/material_data.h"
#include "common/export.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API MaterialSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] std::string serialize(const MaterialData& data) const;
        [[nodiscard]] MaterialData deserialize(
            std::string_view contents,
            std::string_view source = "<memory>") const;

        void save(
            const MaterialData& data,
            const std::filesystem::path& path) const;
        [[nodiscard]] MaterialData load(const std::filesystem::path& source_path) const;
    };
}
