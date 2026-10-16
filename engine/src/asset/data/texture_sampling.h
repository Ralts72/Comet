#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

namespace Comet {
    // 解码基础 mip 到线性颜色；CPU 烘焙与路径追踪共用相同的地址／过滤语义。
    struct COMET_API CpuTextureImage {
        uint32_t width = 0, height = 0;
        SamplerDesc sampler;
        std::vector<Math::Vec4> pixels;

        [[nodiscard]] static Result<CpuTextureImage> decode(
            const TextureData& source, std::size_t remaining_bytes);
        [[nodiscard]] Math::Vec4 sample(Math::Vec2 uv) const;

    private:
        [[nodiscard]] Math::Vec4 texel(int x, int y) const;
    };
}
