#pragma once

#include "common/export.h"

#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace Comet {
    enum class TextureColorSpace : std::uint8_t {
        Srgb,
        Linear
    };

    [[nodiscard]] COMET_API std::string_view to_string(
        TextureColorSpace value) noexcept;
    [[nodiscard]] COMET_API std::optional<TextureColorSpace>
    texture_color_space_from_string(std::string_view value) noexcept;

    struct TextureImportSettings {
        TextureColorSpace color_space = TextureColorSpace::Srgb;
        bool flip_y = false;

        auto operator<=>(const TextureImportSettings&) const noexcept = default;
    };

    using AssetImportSettings = std::variant<
        std::monostate,
        TextureImportSettings>;
}
