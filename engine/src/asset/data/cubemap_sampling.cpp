#include "asset/data/cubemap_sampling.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>

namespace Comet {
    namespace {
        Math::Vec3 read_pixel(const TextureData& data, const std::size_t offset) {
            if(data.format == Format::R16G16B16A16_SFLOAT) {
                Math::Half4 packed;
                std::memcpy(&packed, data.pixels.data() + offset, 8);
                return Math::Vec3(Math::unpack_half(packed));
            }
            Math::Vec4 pixel;
            std::memcpy(&pixel, data.pixels.data() + offset, 16);
            return Math::Vec3(pixel);
        }
    }

    Result<std::size_t> CpuCubeSampler::validate(const TextureData& data) {
        using Checked = Result<std::size_t>;
        if(!data.cubemap || data.width <= 0 || data.width > 2048 || data.width != data.height
            || !std::has_single_bit(static_cast<std::uint32_t>(data.width)) || data.mip_levels == 0
            || data.mip_levels > static_cast<std::uint32_t>(
                   std::bit_width(static_cast<std::uint32_t>(data.width)))
            || (data.format != Format::R16G16B16A16_SFLOAT
                && data.format != Format::R32G32B32A32_SFLOAT))
            return Checked::failure(
                "CPU cubemap sampling requires square power-of-two float16/32 faces (1..2048)");
        const std::size_t texel_size = data.format == Format::R16G16B16A16_SFLOAT ? 8 : 16;
        std::size_t count = 0;
        for(std::uint32_t mip = 0; mip < data.mip_levels; ++mip) {
            const auto size = static_cast<std::size_t>(data.width >> mip);
            count += size * size * 6;
        }
        if(data.pixels.size() != count * texel_size)
            return Checked::failure(
                "CPU cubemap sampling payload size does not match its mip chain");
        for(std::size_t index = 0; index < count; ++index) {
            const auto pixel = read_pixel(data, index * texel_size);
            if(!Math::is_finite(pixel) || Math::any_less(pixel, Math::Vec3(0)))
                return Checked::failure(
                    "CPU cubemap sampling requires finite nonnegative radiance");
        }
        return Checked::success(count * sizeof(Math::Vec3));
    }

    CpuCubeSampler::CpuCubeSampler(const TextureData& data)
        : m_data(data), m_levels(data.mip_levels) {
        const std::size_t texel_size = data.format == Format::R16G16B16A16_SFLOAT ? 8 : 16;
        std::size_t offset = 0;
        for(std::uint32_t mip = 0; mip < data.mip_levels; ++mip) {
            m_offsets.push_back(offset);
            const auto size = static_cast<std::size_t>(data.width >> mip);
            offset += size * size * 6 * texel_size;
        }
    }

    Math::Vec3 CpuCubeSampler::face_direction(const int face, const float x, const float y) {
        const std::array<Math::Vec3, 6> directions{
            {{1, -y, -x}, {-1, -y, x}, {x, 1, y}, {x, -1, -y}, {x, -y, 1}, {-x, -y, -1}}};
        return Math::normalize(directions[face]);
    }

    CpuCubeSampler::Coordinate CpuCubeSampler::coordinate(const Math::Vec3 d) {
        const auto a = Math::abs(d);
        if(a.x >= a.y && a.x >= a.z) {
            if(d.x > 0)
                return {0, {-d.z / a.x, -d.y / a.x}};
            return {1, {d.z / a.x, -d.y / a.x}};
        }
        if(a.y >= a.z) {
            if(d.y > 0)
                return {2, {d.x / a.y, d.z / a.y}};
            return {3, {d.x / a.y, -d.z / a.y}};
        }
        if(d.z > 0)
            return {4, {d.x / a.z, -d.y / a.z}};
        return {5, {-d.x / a.z, -d.y / a.z}};
    }

    Math::Vec3 CpuCubeSampler::sample(const Math::Vec3 direction, float lod) {
        lod = std::clamp(lod, 0.0f, float(m_data.mip_levels - 1));
        const auto low = static_cast<std::uint32_t>(lod);
        const auto high = std::min(low + 1, m_data.mip_levels - 1);
        const auto c = coordinate(direction);
        const auto first = sample_level(c, low);
        if(low == high || lod == float(low))
            return first;
        return Math::mix(first, sample_level(c, high), lod - low);
    }

    Math::Vec3 CpuCubeSampler::texel(int face, int x, int y, const std::uint32_t mip) const {
        const int size = m_data.width >> mip;
        // 跨面采样重新投影到相邻面，避免 clamp 在边缘引入接缝。
        if(x < 0 || y < 0 || x >= size || y >= size) {
            const auto next = coordinate(
                face_direction(face, 2.0f * (x + 0.5f) / size - 1, 2.0f * (y + 0.5f) / size - 1));
            face = next.face;
            x = std::clamp(int((next.uv.x * 0.5f + 0.5f) * size), 0, size - 1);
            y = std::clamp(int((next.uv.y * 0.5f + 0.5f) * size), 0, size - 1);
        }
        return m_levels[mip][(std::size_t(face) * size + y) * size + x];
    }

    Math::Vec3 CpuCubeSampler::sample_level(const Coordinate c, const std::uint32_t mip) {
        auto& level = m_levels[mip];
        if(level.empty()) {
            const auto size = m_data.width >> mip;
            const std::size_t texel_size = m_data.format == Format::R16G16B16A16_SFLOAT ? 8 : 16;
            level.resize(std::size_t(size) * size * 6);
            for(std::size_t index = 0; index < level.size(); ++index)
                level[index] = read_pixel(m_data, m_offsets[mip] + index * texel_size);
        }
        const auto p = (c.uv * 0.5f + 0.5f) * float(m_data.width >> mip) - 0.5f;
        const int x = int(std::floor(p.x)), y = int(std::floor(p.y));
        return Math::mix(Math::mix(texel(c.face, x, y, mip), texel(c.face, x + 1, y, mip), p.x - x),
            Math::mix(texel(c.face, x, y + 1, mip), texel(c.face, x + 1, y + 1, mip), p.x - x),
            p.y - y);
    }

    std::size_t CpuCubeSampler::decoded_bytes() const {
        std::size_t bytes = 0;
        for(const auto& level : m_levels)
            bytes += level.size() * sizeof(Math::Vec3);
        return bytes;
    }
}
