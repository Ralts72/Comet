#include "asset/import/texture_importer.h"
#include "asset/data/texture_data.h"
#include "common/file_io.h"

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {
    struct ResizeContext {
        std::size_t remaining_bytes;
        std::uint8_t* output;
        int width;
    };
    struct alignas(std::max_align_t) ResizeAllocation {
        std::size_t bytes;
    };
    void* allocate_resize(const std::size_t bytes, void* user_data) {
        auto& context = *static_cast<ResizeContext*>(user_data);
        if(context.remaining_bytes < sizeof(ResizeAllocation)
            || bytes > context.remaining_bytes - sizeof(ResizeAllocation))
            return nullptr;
        const auto size = sizeof(ResizeAllocation) + bytes;
        auto* allocation = static_cast<ResizeAllocation*>(std::malloc(size));
        if(!allocation)
            return nullptr;
        allocation->bytes = size;
        context.remaining_bytes -= size;
        return allocation + 1;
    }
    void free_resize(void* pointer, void* user_data) {
        if(!pointer)
            return;
        auto* allocation = static_cast<ResizeAllocation*>(pointer) - 1;
        static_cast<ResizeContext*>(user_data)->remaining_bytes += allocation->bytes;
        std::free(allocation);
    }
}

// 实现只在导入适配内编译；每次缩放的临时分配受该导入任务的预算约束。
#define STBIR_STATIC
#define STBIR_MALLOC(bytes, user_data) allocate_resize(bytes, user_data)
#define STBIR_FREE(pointer, user_data) free_resize(pointer, user_data)
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#undef STB_IMAGE_RESIZE_IMPLEMENTATION
#undef STBIR_FREE
#undef STBIR_MALLOC
#undef STBIR_STATIC

namespace Comet {
    namespace {
        constexpr std::size_t DECODE_OVERHEAD_BYTES = 16ull * 1024 * 1024;

        void write_normal_row(const void* source, const int count, const int row, void* user_data) {
            const auto& context = *static_cast<ResizeContext*>(user_data);
            const auto* pixels = static_cast<const float*>(source);
            auto* destination = context.output + std::size_t(row) * context.width * 4;
            for(int x = 0; x < count; ++x) {
                std::array<float, 4> value{
                    pixels[x * 4], pixels[x * 4 + 1], pixels[x * 4 + 2], pixels[x * 4 + 3]};
                std::array<float, 3> direction{
                    value[0] * 2 - 1, value[1] * 2 - 1, value[2] * 2 - 1};
                const float length =
                    std::sqrt(direction[0] * direction[0] + direction[1] * direction[1]
                              + direction[2] * direction[2]);
                if(length > 1e-6f) {
                    for(unsigned channel = 0; channel < 3; ++channel)
                        value[channel] = direction[channel] / length * 0.5f + 0.5f;
                } else {
                    value[0] = value[1] = 0.5f;
                    value[2] = 1.0f;
                }
                for(unsigned channel = 0; channel < 4; ++channel)
                    destination[x * 4 + channel] = static_cast<std::uint8_t>(
                        std::lround(std::clamp(value[channel], 0.0f, 1.0f) * 255.0f));
            }
        }

        Result<void> generate_mipmaps(
            TextureData& data, const bool normal_map, const std::size_t memory_budget) {
            const bool srgb = data.format == Format::R8G8B8A8_SRGB;
            data.mip_levels =
                std::bit_width(static_cast<unsigned>(std::max(data.width, data.height)));
            std::size_t total = 0;
            for(unsigned mip = 0; mip < data.mip_levels; ++mip)
                total += std::size_t(std::max(data.width >> mip, 1))
                         * std::max(data.height >> mip, 1) * 4;
            data.pixels.resize(total);
            if(data.pixels.capacity() > memory_budget)
                return Result<void>::failure("Texture mip chain exceeds its CPU memory budget");
            std::size_t source_offset = 0;
            int width = data.width;
            int height = data.height;
            for(unsigned mip = 1; mip < data.mip_levels; ++mip) {
                const int next_width = std::max(width / 2, 1);
                const int next_height = std::max(height / 2, 1);
                const auto destination_offset = source_offset + std::size_t(width) * height * 4;
                ResizeContext context{memory_budget - data.pixels.capacity(),
                    data.pixels.data() + destination_offset, next_width};
                STBIR_RESIZE resize;
                stbir_resize_init(&resize, data.pixels.data() + source_offset, width, height, 0,
                    context.output, next_width, next_height, 0, STBIR_RGBA_NO_AW,
                    srgb ? STBIR_TYPE_UINT8_SRGB : STBIR_TYPE_UINT8);
                // 保留面积滤波与独立 Alpha；不采用 stb 默认的 Mitchell／Alpha 加权。
                stbir_set_filters(&resize, STBIR_FILTER_BOX, STBIR_FILTER_BOX);
                stbir_set_user_data(&resize, &context);
                if(normal_map) {
                    // 按行消费浮点结果，避免先量化或分配整级浮点图像。
                    stbir_set_datatypes(&resize, STBIR_TYPE_UINT8, STBIR_TYPE_FLOAT);
                    stbir_set_pixel_callbacks(&resize, nullptr, write_normal_row);
                }
                if(!stbir_resize_extended(&resize))
                    return Result<void>::failure(
                        "Texture mip filtering failed or exceeded its CPU memory budget");
                source_offset = destination_offset;
                width = next_width;
                height = next_height;
            }
            return Result<void>::success();
        }

        Result<std::size_t> estimate_working_bytes(const std::size_t source_bytes, const int width,
            const int height, const AssetImportLimits& limits) {
            // 解码 RGBA + 完整 mip 链；1×N 的链大小也小于基级的两倍。
            constexpr std::size_t bytes_per_pixel = 12;
            if(source_bytes > limits.source_bytes || width <= 0 || height <= 0)
                return Result<std::size_t>::failure(
                    "Texture source exceeds its configured limit or has invalid dimensions");
            // BOX／RGBA 的系数与行缓冲按轴长预留，避免超宽图像只按像素数低估。
            // 这是调度预留；实际分配仍由每次调用的 allocator 强制限制。
            constexpr std::size_t sampler_bytes_per_axis_pixel = 128;
            const auto axis_pixels = static_cast<std::size_t>(width) + height;
            if(limits.texture_working_bytes <= DECODE_OVERHEAD_BYTES
                || axis_pixels > (limits.texture_working_bytes - DECODE_OVERHEAD_BYTES)
                                     / sampler_bytes_per_axis_pixel)
                return Result<std::size_t>::failure(
                    "Texture exceeds its configured CPU working-set budget");
            const auto overhead =
                DECODE_OVERHEAD_BYTES + axis_pixels * sampler_bytes_per_axis_pixel;
            if(source_bytes > limits.texture_working_bytes - overhead)
                return Result<std::size_t>::failure(
                    "Texture exceeds its configured CPU working-set budget");
            const auto available = limits.texture_working_bytes - overhead - source_bytes;
            const auto max_pixels = available / bytes_per_pixel;
            if(static_cast<std::size_t>(width) > max_pixels / static_cast<std::size_t>(height))
                return Result<std::size_t>::failure(
                    "Texture exceeds its configured CPU working-set budget");
            const auto pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            return Result<std::size_t>::success(source_bytes + pixels * bytes_per_pixel + overhead);
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
        if(settings.generate_mipmaps) {
            const auto result =
                generate_mipmaps(data, settings.normal_map, estimate.value() - encoded.size());
            if(!result)
                return Result<TextureData>::failure(result.error());
        }
        return Result<TextureData>::success(std::move(data));
    }
}
