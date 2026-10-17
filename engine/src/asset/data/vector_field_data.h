#pragma once
#include "asset/content_fwd.h"
#include "core/geometry.h"
#include "common/result.h"
#include <filesystem>
#include <vector>

namespace Comet {
    // X 连续、Y 次之、Z 为切片；向量是世界空间加速度，界外为零。
    struct COMET_API VectorFieldData {
        Math::Vec3u dimensions{2};
        BoundingBox bounds{{-1, -1, -1}, {1, 1, 1}};
        std::vector<Math::Vec3> values;
        Result<void> validate() const;
        Math::Vec3 sample(Math::Vec3 position) const;
        static Result<VectorFieldData> load(const std::filesystem::path&);
        static Result<VectorFieldData> deserialize(
            std::string_view, std::string_view source = "<memory>");
        Result<std::string> serialize() const;
    };
}
