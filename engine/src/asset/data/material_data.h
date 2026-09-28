#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Comet {
    // 不可变的场景运行快照，不进入资产序列化。
    struct MaterialOverrides {
        uint64_t instance_id = 0;
        AssetHandle material;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, Math::Vec4> vector_properties;
    };

    // 场景只借用校验能力；布局和运行时材质仍由资源／渲染侧持有。
    class COMET_API MaterialParameterValidator {
    public:
        virtual ~MaterialParameterValidator() = default;
        [[nodiscard]] virtual Result<void> validate(const MaterialOverrides& overrides) const = 0;
    };

    struct MaterialData {
        std::string template_name;
        AssetHandle shader_program;
        std::map<std::string, AssetHandle> texture_properties;
        std::map<std::string, float> scalar_properties;
        std::map<std::string, Math::Vec4> vector_properties;

        bool operator==(const MaterialData&) const noexcept = default;
    };

    [[nodiscard]] COMET_API std::vector<AssetHandle> get_asset_dependencies(
        const MaterialData& data);
}
