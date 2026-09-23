#pragma once

#include "common/export.h"
#include "common/result.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    struct MaterialShaderProgram {
        std::vector<uint32_t> vertex;
        std::vector<uint32_t> fragment;
        std::string vertex_entry = "main";
        std::string fragment_entry = "main";
    };

    // Key 是程序名，不是材质模板名；未提供的程序保持原版本。
    using MaterialShaders = std::map<std::string, MaterialShaderProgram, std::less<>>;

    struct MaterialShaderDefinition {
        std::string_view name;
        std::string_view material;
    };

    [[nodiscard]] COMET_API std::span<const MaterialShaderDefinition> builtin_material_shaders();
    [[nodiscard]] COMET_API MaterialShaders default_material_shaders();
    [[nodiscard]] COMET_API Result<void> validate_material_shaders(const MaterialShaders& shaders);
    // 调用方在校验或发布成功后合并；保留未参与更新的程序。
    COMET_API void merge_material_shaders(MaterialShaders& destination, MaterialShaders updates);
}
