#include "asset/import/texture_importer.h"
#include "asset/data/texture_data.h"

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace Comet {
    namespace {
        constexpr std::size_t MAX_SOURCE_BYTES = 512ull * 1024 * 1024;
        constexpr std::size_t DECODE_OVERHEAD_BYTES = 16ull * 1024 * 1024;

        Result<std::size_t> estimate_working_bytes(
            const std::size_t source_bytes, const int width, const int height) {
            constexpr std::size_t bytes_per_pixel = 12;
            if(source_bytes > MAX_SOURCE_BYTES || width <= 0 || height <= 0)
                return Result<std::size_t>::failure(
                    "Texture source exceeds 512 MiB or has invalid dimensions");
            const auto available =
                TextureImporter::MAX_WORKING_BYTES - DECODE_OVERHEAD_BYTES - source_bytes;
            const auto max_pixels = available / bytes_per_pixel;
            if(static_cast<std::size_t>(width) > max_pixels / static_cast<std::size_t>(height))
                return Result<std::size_t>::failure(
                    "Texture exceeds its 1 GiB CPU working-set budget");
            const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            return Result<std::size_t>::success(
                source_bytes + pixels * bytes_per_pixel + DECODE_OVERHEAD_BYTES);
        }
    }

    Result<std::size_t> TextureImporter::working_bytes(const std::filesystem::path& source_path) {
        std::error_code error;
        const auto source_size = std::filesystem::file_size(source_path, error);
        if(error || source_size > MAX_SOURCE_BYTES)
            return Result<std::size_t>::failure(
                "Cannot read texture or source exceeds 512 MiB: " + source_path.string());
        int width = 0;
        int height = 0;
        int channels = 0;
        if(!stbi_info(source_path.string().c_str(), &width, &height, &channels))
            return Result<std::size_t>::failure(
                "Cannot inspect texture source: " + source_path.string());
        return estimate_working_bytes(source_size, width, height);
    }

    Result<TextureData> TextureImporter::import(const std::filesystem::path& source_path,
        const TextureImportSettings& settings, const std::size_t memory_budget) const {
        auto estimated = working_bytes(source_path);
        if(!estimated)
            return Result<TextureData>::failure(estimated.error());
        if(estimated.value() > memory_budget)
            return Result<TextureData>::failure("Texture exceeds its reserved CPU memory budget");

        std::error_code error;
        const auto source_size = std::filesystem::file_size(source_path, error);
        if(error || source_size > MAX_SOURCE_BYTES)
            return Result<TextureData>::failure(
                "Cannot read texture or source size changed: " + source_path.string());
        std::vector<stbi_uc> source(source_size);
        std::ifstream input(source_path, std::ios::binary);
        if(!input.read(reinterpret_cast<char*>(source.data()), source.size())
            || input.peek() != std::ifstream::traits_type::eof())
            return Result<TextureData>::failure(
                "Cannot read texture or source size changed: " + source_path.string());

        int width = 0;
        int height = 0;
        int channels = 0;
        const auto byte_count = static_cast<int>(source.size());
        if(!stbi_info_from_memory(source.data(), byte_count, &width, &height, &channels))
            return Result<TextureData>::failure(
                "Cannot inspect texture source: " + source_path.string());
        auto current_estimate = estimate_working_bytes(source.size(), width, height);
        if(!current_estimate || current_estimate.value() > memory_budget)
            return Result<TextureData>::failure("Texture exceeds its reserved CPU memory budget");
        const int inspected_width = width;
        const int inspected_height = height;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(
                source.data(), byte_count, &width, &height, nullptr, STBI_rgb_alpha),
            stbi_image_free);
        if(!pixels) {
            const char* reason = stbi_failure_reason();
            return Result<TextureData>::failure(
                "Failed to import texture '" + source_path.string()
                + "': " + (reason ? reason : "unknown stb_image error"));
        }
        if(width != inspected_width || height != inspected_height) {
            return Result<TextureData>::failure(
                "Failed to import texture '" + source_path.string()
                + "': decoded dimensions differ from inspected dimensions");
        }

        constexpr int output_channels = STBI_rgb_alpha;
        const std::size_t size =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * output_channels;
        Format format = Format::R8G8B8A8_UNORM;
        if(settings.color_space == TextureColorSpace::Srgb) {
            format = Format::R8G8B8A8_SRGB;
        }
        TextureData data{.width = width, .height = height, .format = format};
        data.pixels.assign(pixels.get(), pixels.get() + size);
        if(settings.flip_y) {
            const std::size_t row_size = static_cast<std::size_t>(width) * output_channels;
            for(int row = 0; row < height / 2; ++row) {
                const auto top = data.pixels.begin() + static_cast<std::ptrdiff_t>(row * row_size);
                const auto bottom = data.pixels.begin()
                                    + static_cast<std::ptrdiff_t>((height - row - 1) * row_size);
                std::swap_ranges(top, top + static_cast<std::ptrdiff_t>(row_size), bottom);
            }
        }
        return Result<TextureData>::success(std::move(data));
    }
}
