#include "asset/texture_importer.h"

#include <stb_image.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace Comet {
    TextureData TextureImporter::import(
        const std::filesystem::path& source_path) const {
        int width = 0;
        int height = 0;
        const std::string source = source_path.string();
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load(
                source.c_str(),
                &width,
                &height,
                nullptr,
                STBI_rgb_alpha),
            stbi_image_free);
        if(!pixels) {
            const char* reason = stbi_failure_reason();
            throw std::runtime_error(
                "Failed to import texture '" + source + "': "
                + (reason ? reason : "unknown stb_image error"));
        }
        if(width <= 0 || height <= 0) {
            throw std::runtime_error(
                "Failed to import texture '" + source
                + "': decoded dimensions must be greater than zero");
        }

        constexpr int output_channels = STBI_rgb_alpha;
        const std::size_t size = static_cast<std::size_t>(width)
            * static_cast<std::size_t>(height) * output_channels;
        TextureData data{
            .width = width,
            .height = height,
            .channels = output_channels,
            .format = Format::R8G8B8A8_UNORM
        };
        data.pixels.assign(pixels.get(), pixels.get() + size);
        return data;
    }
}
