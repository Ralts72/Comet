#pragma once

#include "asset/metadata.h"
#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Comet {
    class COMET_API MetadataSerializer final {
    public:
        static constexpr std::uint32_t FORMAT_VERSION = 3;

        [[nodiscard]] Result<std::string> serialize(const AssetMetadata& metadata) const;
        [[nodiscard]] Result<AssetMetadata> deserialize(
            std::string_view contents, std::string_view source = "<memory>") const;

        [[nodiscard]] Result<void> save(
            const AssetMetadata& metadata, const std::filesystem::path& path) const;
        [[nodiscard]] Result<AssetMetadata> load(const std::filesystem::path& path) const;
    };
}
