#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

namespace Comet {
    // 解码为线性颜色；可限定低频求光照的尺寸，优先已有 mip，无 mip 时按面积缩小。
    struct COMET_API CpuTextureImage {
        uint32_t width = 0, height = 0;
        SamplerDesc sampler;
        std::vector<Math::Vec4> pixels;

        [[nodiscard]] static Result<CpuTextureImage> decode(const TextureData& source,
            std::size_t remaining_bytes, uint32_t maximum_dimension = 8192);
        [[nodiscard]] Math::Vec4 sample(Math::Vec2 uv) const;

    private:
        [[nodiscard]] Math::Vec4 texel(int x, int y) const;
    };
}
