#include "asset/data/texture_sampling.h"

#include <glm/gtc/packing.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Comet {
    Result<CpuTextureImage> CpuTextureImage::decode(
        const TextureData& source, const std::size_t remaining_bytes) {
        using Decoded = Result<CpuTextureImage>;
        if(source.cubemap || source.width <= 0 || source.height <= 0 || source.width > 8192
            || source.height > 8192 || source.mip_levels == 0)
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
        const auto count = std::size_t(source.width) * source.height;
        if(source.pixels.size() < count * texel_size
            || count > remaining_bytes / sizeof(Math::Vec4))
            return Decoded::failure("CPU texture exceeds its payload or memory budget");
        CpuTextureImage result;
        result.width = static_cast<uint32_t>(source.width);
        result.height = static_cast<uint32_t>(source.height);
        result.sampler = source.sampler.value_or(SamplerDesc{});
        result.pixels.resize(count);
        for(std::size_t index = 0; index < count; ++index) {
            auto& pixel = result.pixels[index];
            const auto* bytes = source.pixels.data() + index * texel_size;
            if(texel_size == 4) {
                for(int channel = 0; channel < 4; ++channel)
                    pixel[channel] = bytes[channel] / 255.0f;
                if(source.format == Format::R8G8B8A8_SRGB) {
                    for(int channel = 0; channel < 3; ++channel) {
                        const float value = pixel[channel];
                        pixel[channel] = value <= 0.04045f
                                             ? value / 12.92f
                                             : std::pow((value + 0.055f) / 1.055f, 2.4f);
                    }
                }
            } else if(texel_size == 8) {
                glm::u16vec4 packed;
                std::memcpy(&packed, bytes, 8);
                pixel = glm::unpackHalf(packed);
            } else {
                std::memcpy(&pixel, bytes, 16);
            }
            if(!Math::is_finite(pixel) || glm::any(glm::lessThan(pixel, Math::Vec4(0))))
                return Decoded::failure("CPU texture contains invalid radiance");
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
        return glm::mix(glm::mix(texel(x, y), texel(x + 1, y), p.x - x),
            glm::mix(texel(x, y + 1), texel(x + 1, y + 1), p.x - x), p.y - y);
    }
}
