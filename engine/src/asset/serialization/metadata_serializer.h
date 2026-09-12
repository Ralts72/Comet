#pragma once

#include "asset/metadata.h"
#include "common/export.h"
#include "asset/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API MetadataSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 2;

        [[nodiscard]] AssetResult<std::string> serialize(
            const AssetMetadata& metadata) const;
        [[nodiscard]] AssetResult<AssetMetadata> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;

        [[nodiscard]] AssetResult<void> save(
            const AssetMetadata& metadata, const std::filesystem::path& path) const;
        [[nodiscard]] AssetResult<AssetMetadata> load(
            const std::filesystem::path& path) const;
    };
}
