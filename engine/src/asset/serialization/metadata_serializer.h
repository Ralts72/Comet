#pragma once

#include "asset/metadata.h"
#include "common/export.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API AssetMetadataSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 1;

        [[nodiscard]] std::string serialize(const AssetMetadata& metadata) const;
        [[nodiscard]] AssetMetadata deserialize(
            std::string_view contents,
            std::string_view source = "<memory>") const;

        void save(
            const AssetMetadata& metadata,
            const std::filesystem::path& path) const;
        [[nodiscard]] AssetMetadata load(const std::filesystem::path& path) const;
    };
}
