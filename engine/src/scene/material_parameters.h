#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <map>
#include <string>

namespace Comet {
    // 不可变的场景运行快照，不进入资产或场景序列化。
    struct MaterialOverrides {
        uint64_t instance_id = 0;
        AssetHandle material;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, Math::Vec4> vector_properties;
    };

    // Scene 借用已发布材质契约的校验能力，不接触布局或 GPU 对象。
    class COMET_API MaterialParameterValidator {
    public:
        virtual ~MaterialParameterValidator() = default;
        [[nodiscard]] virtual Result<void> validate(const MaterialOverrides& overrides) const = 0;
    };
}
