#include "asset/import/texture_importer.h"
#include "asset/data/texture_data.h"
#include "common/file_io.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace Comet {
    namespace {
        constexpr std::size_t DECODE_OVERHEAD_BYTES = 16ull * 1024 * 1024;

        float decode_srgb(const float value) {
            if(value <= 0.04045f)
                return value / 12.92f;
            return std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        float encode_srgb(const float value) {
            if(value <= 0.0031308f)
                return value * 12.92f;
            return 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        std::array<std::uint8_t, 4> filter_area(const std::uint8_t* source, const int width,
            const int height, const float left, const float top, const float right,
            const float bottom, const bool srgb, const bool normal_map) {
            static const auto linear_colors = [] {
                std::array<float, 256> values{};
                for(std::size_t i = 0; i < values.size(); ++i)
                    values[i] = decode_srgb(float(i) / 255.0f);
                return values;
            }();
            std::array<float, 4> sum{};
            for(int y = int(top); y < std::min(int(std::ceil(bottom)), height); ++y) {
                const float weight_y = std::min(bottom, float(y + 1)) - std::max(top, float(y));
                for(int x = int(left); x < std::min(int(std::ceil(right)), width); ++x) {
                    const float weight =
                        weight_y * (std::min(right, float(x + 1)) - std::max(left, float(x)));
                    const auto pixel = (std::size_t(y) * width + x) * 4;
                    for(unsigned channel = 0; channel < 4; ++channel) {
                        const auto encoded = source[pixel + channel];
                        float value = float(encoded) / 255.0f;
                        if(srgb && channel < 3)
                            value = linear_colors[encoded];
                        sum[channel] += value * weight;
                    }
                }
            }
            const float area = (right - left) * (bottom - top);
            for(auto& value : sum)
                value /= area;
            if(normal_map) {
                std::array<float, 3> direction{sum[0] * 2 - 1, sum[1] * 2 - 1, sum[2] * 2 - 1};
                const float length =
                    std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]
                              + direction[2] * direction[2]);
                if(length > 1e-6f) {
                    for(unsigned channel = 0; channel < 3; ++channel)
                        sum[channel] = direction[channel] / length * 0.5f + 0.5f;
                } else {
                    sum[0] = sum[1] = 0.5f;
                    sum[2] = 1.0f;
                }
            }
            std::array<std::uint8_t, 4> result{};
            for(unsigned channel = 0; channel < 4; ++channel) {
                float value = sum[channel];
                if(srgb && channel < 3)
                    value = encode_srgb(value);
                result[channel] =
                    static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
            }
            return result;
        }

        // 面积滤波保留奇数边缘像素；RGB 在线性空间平均，Alpha 始终是线性值。
        void generate_mipmaps(TextureData& data, const bool normal_map) {
            const bool srgb = data.format == Format::R8G8B8A8_SRGB;
            data.mip_levels =
                std::bit_width(static_cast<unsigned>(std::max(data.width, data.height)));
            std::size_t total = 0;
            for(unsigned mip = 0; mip < data.mip_levels; ++mip)
                total += std::size_t(std::max(data.width >> mip, 1))
                         * std::max(data.height >> mip, 1) * 4;
            data.pixels.resize(total);
            std::size_t source_offset = 0;
            int width = data.width;
            int height = data.height;
            for(unsigned mip = 1; mip < data.mip_levels; ++mip) {
                const int next_width = std::max(width / 2, 1);
                const int next_height = std::max(height / 2, 1);
                const auto destination_offset = source_offset + std::size_t(width) * height * 4;
                const float scale_x = float(width) / next_width;
                const float scale_y = float(height) / next_height;
                for(int y = 0; y < next_height; ++y) {
                    for(int x = 0; x < next_width; ++x) {
                        const auto pixel = filter_area(data.pixels.data() + source_offset, width,
                            height, x * scale_x, y * scale_y, (x + 1) * scale_x, (y + 1) * scale_y,
                            srgb, normal_map);
                        const auto offset =
                            destination_offset + (std::size_t(y) * next_width + x) * 4;
                        std::copy(pixel.begin(), pixel.end(), data.pixels.data() + offset);
                    }
                }
                source_offset = destination_offset;
                width = next_width;
                height = next_height;
            }
        }

        Result<std::size_t> estimate_working_bytes(const std::size_t source_bytes, const int width,
            const int height, const AssetImportLimits& limits) {
            // 解码 RGBA + 完整 mip 链；1×N 的链大小也小于基级的两倍。
            constexpr std::size_t bytes_per_pixel = 12;
            if(source_bytes > limits.source_bytes || width <= 0 || height <= 0)
                return Result<std::size_t>::failure(
                    "Texture source exceeds its configured limit or has invalid dimensions");
            if(limits.texture_working_bytes <= DECODE_OVERHEAD_BYTES
                || source_bytes > limits.texture_working_bytes - DECODE_OVERHEAD_BYTES)
                return Result<std::size_t>::failure(
                    "Texture exceeds its configured CPU working-set budget");
            const auto available =
                limits.texture_working_bytes - DECODE_OVERHEAD_BYTES - source_bytes;
            const auto max_pixels = available / bytes_per_pixel;
            if(static_cast<std::size_t>(width) > max_pixels / static_cast<std::size_t>(height))
                return Result<std::size_t>::failure(
                    "Texture exceeds its configured CPU working-set budget");
            const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            return Result<std::size_t>::success(
                source_bytes + pixels * bytes_per_pixel + DECODE_OVERHEAD_BYTES);
        }
    }

    Result<std::size_t> TextureImporter::working_bytes(
        const std::filesystem::path& source_path, const AssetImportLimits& limits) {
        std::error_code error;
        const auto source_size = std::filesystem::file_size(source_path, error);
        if(error || source_size > limits.source_bytes
            || source_size > static_cast<std::uintmax_t>(std::numeric_limits<int>::max()))
            return Result<std::size_t>::failure(
                "Cannot read texture or source exceeds its configured limit: "
                + source_path.string());
        int width = 0;
        int height = 0;
        int channels = 0;
        if(!stbi_info(source_path.string().c_str(), &width, &height, &channels))
            return Result<std::size_t>::failure(
                "Cannot inspect texture source: " + source_path.string());
        return estimate_working_bytes(source_size, width, height, limits);
    }

    Result<TextureData> TextureImporter::import(const std::filesystem::path& source_path,
        const TextureImportSettings& settings, const std::size_t memory_budget,
        const AssetImportLimits& limits) const {
        auto estimated = working_bytes(source_path, limits);
        if(!estimated)
            return Result<TextureData>::failure(estimated.error());
        if(estimated.value() > memory_budget)
            return Result<TextureData>::failure("Texture exceeds its reserved CPU memory budget");

        auto source = read_binary_file(source_path, std::min(limits.source_bytes, memory_budget));
        if(!source)
            return Result<TextureData>::failure(source.error());
        auto decoded =
            import(std::span<const std::byte>(source.value()), settings, memory_budget, limits);
        if(!decoded)
            return Result<TextureData>::failure(
                "Failed to import texture '" + source_path.string() + "': " + decoded.error());
        return decoded;
    }

    Result<std::size_t> TextureImporter::working_bytes(
        std::span<const std::byte> encoded, const AssetImportLimits& limits) {
        if(encoded.empty() || encoded.size() > limits.source_bytes
            || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            return Result<std::size_t>::failure(
                "Encoded texture is empty or exceeds its input budget");
        int width = 0, height = 0, channels = 0;
        if(!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
               static_cast<int>(encoded.size()), &width, &height, &channels))
            return Result<std::size_t>::failure("Cannot inspect encoded texture");
        return estimate_working_bytes(encoded.size(), width, height, limits);
    }

    Result<TextureData> TextureImporter::import(std::span<const std::byte> encoded,
        const TextureImportSettings& settings, std::size_t memory_budget,
        const AssetImportLimits& limits) const {
        const auto estimate = working_bytes(encoded, limits);
        if(!estimate)
            return Result<TextureData>::failure(estimate.error());
        if(estimate.value() > memory_budget)
            return Result<TextureData>::failure("Texture exceeds its reserved CPU memory budget");
        int width = 0, height = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(encoded.data()),
                static_cast<int>(encoded.size()), &width, &height, nullptr, STBI_rgb_alpha),
            stbi_image_free);
        if(!pixels) {
            const char* reason = stbi_failure_reason();
            return Result<TextureData>::failure(std::string("Cannot decode texture: ")
                                                + (reason ? reason : "unknown stb_image error"));
        }
        const auto decoded_estimate = estimate_working_bytes(encoded.size(), width, height, limits);
        if(!decoded_estimate || decoded_estimate.value() > estimate.value())
            return Result<TextureData>::failure("Decoded texture exceeds its inspected dimensions");

        constexpr int output_channels = STBI_rgb_alpha;
        const std::size_t size =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * output_channels;
        Format format = Format::R8G8B8A8_UNORM;
        if(!settings.normal_map && settings.color_space == TextureColorSpace::Srgb) {
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
        pixels.reset();
        if(settings.generate_mipmaps)
            generate_mipmaps(data, settings.normal_map);
        return Result<TextureData>::success(std::move(data));
    }
}
