#include "asset/import_settings.h"

namespace Comet {
    std::string_view to_string(const TextureColorSpace value) noexcept {
        switch(value) {
            case TextureColorSpace::Srgb: return "srgb";
            case TextureColorSpace::Linear: return "linear";
        }
        return "srgb";
    }

    std::optional<TextureColorSpace> texture_color_space_from_string(
        const std::string_view value) noexcept {
        if(value == "srgb") return TextureColorSpace::Srgb;
        if(value == "linear") return TextureColorSpace::Linear;
        return std::nullopt;
    }
}
