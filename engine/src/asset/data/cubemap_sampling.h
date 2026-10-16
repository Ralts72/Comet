#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstddef>
#include <vector>

namespace Comet {
    // 借用已验证的浮点 cubemap；每个实例独占惰性解码缓存，不跨 Worker 共享。
    class COMET_API CpuCubeSampler final {
    public:
        explicit CpuCubeSampler(const TextureData& data);

        [[nodiscard]] static Result<std::size_t> validate(const TextureData& data);
        [[nodiscard]] static Math::Vec3 face_direction(int face, float x, float y);
        [[nodiscard]] Math::Vec3 sample(Math::Vec3 direction, float lod = 0);
        [[nodiscard]] std::size_t decoded_bytes() const;

    private:
        struct Coordinate {
            int face;
            Math::Vec2 uv;
        };
        [[nodiscard]] static Coordinate coordinate(Math::Vec3 direction);
        [[nodiscard]] Math::Vec3 texel(int face, int x, int y, std::uint32_t mip) const;
        [[nodiscard]] Math::Vec3 sample_level(Coordinate coordinate, std::uint32_t mip);

        const TextureData& m_data;
        std::vector<std::size_t> m_offsets;
        std::vector<std::vector<Math::Vec3>> m_levels;
    };
}
