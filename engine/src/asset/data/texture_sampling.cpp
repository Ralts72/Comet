#include "asset/data/texture_sampling.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Comet {
    Result<CpuTextureImage> CpuTextureImage::decode(const TextureData& source,
        const std::size_t remaining_bytes, const uint32_t maximum_dimension) {
        using Decoded = Result<CpuTextureImage>;
        if(source.cubemap || source.width <= 0 || source.height <= 0 || source.width > 8192
            || source.height > 8192 || source.mip_levels == 0 || !maximum_dimension
            || maximum_dimension > 8192)
            return Decoded::failure("Invalid CPU texture dimensions");
        std::size_t texel_size = 0;
        if(source.format == Format::R8G8B8A8_UNORM || source.format == Format::R8G8B8A8_SRGB)
            texel_size = 4;
        else if(source.format == Format::R16G16B16A16_SFLOAT)
            texel_size = 8;
        else if(source.format == Format::R32G32B32A32_SFLOAT)
            texel_size = 16;
        if(texel_size == 0)
            return Decoded::failure("Unsupported CPU texture format");
        uint32_t width = uint32_t(source.width), height = uint32_t(source.height);
        std::size_t offset = 0;
        for(uint32_t mip = 1;
            mip < source.mip_levels && std::max(width, height) > maximum_dimension; ++mip) {
            offset += std::size_t(width) * height * texel_size;
            width = std::max(1u, width / 2);
            height = std::max(1u, height / 2);
        }
        if(offset > source.pixels.size()
            || std::size_t(width) * height > (source.pixels.size() - offset) / texel_size)
            return Decoded::failure("CPU texture exceeds its payload");
        CpuTextureImage result;
        const auto dimension = std::max(width, height);
        result.width = dimension > maximum_dimension
                           ? std::max(1u, width * maximum_dimension / dimension)
                           : width;
        result.height = dimension > maximum_dimension
                            ? std::max(1u, height * maximum_dimension / dimension)
                            : height;
        const auto count = std::size_t(result.width) * result.height;
        if(count > remaining_bytes / sizeof(Math::Vec4))
            return Decoded::failure("CPU texture exceeds its memory budget");
        result.sampler = source.sampler.value_or(SamplerDesc{});
        result.pixels.resize(count);
        const auto read = [&](std::size_t index, Math::Vec4& pixel) {
            const auto* bytes = source.pixels.data() + offset + index * texel_size;
            if(texel_size == 4) {
                for(int channel = 0; channel < 4; ++channel)
                    pixel[channel] = bytes[channel] / 255.0f;
                if(source.format == Format::R8G8B8A8_SRGB)
                    for(int channel = 0; channel < 3; ++channel) {
                        const float value = pixel[channel];
                        pixel[channel] = value <= 0.04045f
                                             ? value / 12.92f
                                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
                    }
            } else if(texel_size == 8) {
                Math::Half4 packed;
                std::memcpy(&packed, bytes, 8);
                pixel = Math::unpack_half(packed);
            } else {
                std::memcpy(&pixel, bytes, 16);
            }
            return Math::is_finite(pixel) && Math::all_greater_equal(pixel, Math::Vec4(0));
        };
        if(result.width == width && result.height == height) {
            for(std::size_t index = 0; index < count; ++index)
                if(!read(index, result.pixels[index]))
                    return Decoded::failure("CPU texture contains invalid radiance");
        } else {
            // 没有适用的 mip 时直接按面积求均值，不展开更大的浮点中间图。
            const double scale_x = double(width) / result.width;
            const double scale_y = double(height) / result.height;
            for(uint32_t y = 0; y < result.height; ++y)
                for(uint32_t x = 0; x < result.width; ++x) {
                    const double left = x * scale_x, right = (x + 1) * scale_x;
                    const double top = y * scale_y, bottom = (y + 1) * scale_y;
                    Math::Vec4d sum(0);
                    for(uint32_t sy = uint32_t(top);
                        sy < std::min(height, uint32_t(std::ceil(bottom))); ++sy)
                        for(uint32_t sx = uint32_t(left);
                            sx < std::min(width, uint32_t(std::ceil(right))); ++sx) {
                            Math::Vec4 pixel;
                            if(!read(std::size_t(sy) * width + sx, pixel))
                                return Decoded::failure("CPU texture contains invalid radiance");
                            const double area =
                                (std::min(right, double(sx + 1)) - std::max(left, double(sx)))
                                * (std::min(bottom, double(sy + 1)) - std::max(top, double(sy)));
                            sum += Math::Vec4d(pixel) * area;
                        }
                    result.pixels[std::size_t(y) * result.width + x] =
                        Math::Vec4(sum / (scale_x * scale_y));
                }
        }
        return Decoded::success(std::move(result));
    }

    Math::Vec4 CpuTextureImage::texel(int x, int y) const {
        const auto address = [](int& value, const int size, const SamplerAddressMode mode) {
            if(mode == SamplerAddressMode::ClampToBorder && (value < 0 || value >= size))
                return false;
            if(mode == SamplerAddressMode::Repeat)
                value = (value % size + size) % size;
            else if(mode == SamplerAddressMode::MirrorRepeat) {
                value = (value % (2 * size) + 2 * size) % (2 * size);
                if(value >= size)
                    value = 2 * size - 1 - value;
            } else
                value = std::clamp(value, 0, size - 1);
            return true;
        };
        if(!address(x, int(width), sampler.address_mode_u)
            || !address(y, int(height), sampler.address_mode_v))
            return {0, 0, 0, 1};
        return pixels[std::size_t(y) * width + x];
    }

    Math::Vec4 CpuTextureImage::sample(Math::Vec2 uv) const {
        // 先约化周期坐标，避免有限但极大的源 UV 转整数时溢出。
        for(int axis = 0; axis < 2; ++axis) {
            const auto mode = axis == 0 ? sampler.address_mode_u : sampler.address_mode_v;
            if(mode == SamplerAddressMode::Repeat)
                uv[axis] -= std::floor(uv[axis]);
            else if(mode == SamplerAddressMode::MirrorRepeat)
                uv[axis] = std::fmod(std::fmod(uv[axis], 2.0f) + 2.0f, 2.0f);
            else
                uv[axis] = std::clamp(uv[axis], -1.0f, 2.0f);
        }
        const auto p = uv * Math::Vec2(width, height) - 0.5f;
        if(sampler.mag_filter == Filter::Nearest)
            return texel(int(std::floor(p.x + 0.5f)), int(std::floor(p.y + 0.5f)));
        const int x = int(std::floor(p.x)), y = int(std::floor(p.y));
        return Math::mix(Math::mix(texel(x, y), texel(x + 1, y), p.x - x),
            Math::mix(texel(x, y + 1), texel(x + 1, y + 1), p.x - x), p.y - y);
    }
}
