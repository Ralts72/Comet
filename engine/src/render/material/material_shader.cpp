#include "render/material/material_shader.h"
#include "graphics/pipeline/shader_interface.h"
#include "unlit_color_vert.h"
#include "unlit_color_frag.h"
#include "pbr_vert.h"
#include "pbr_frag.h"

#include <array>
#include <optional>
#include <utility>

namespace Comet {
    std::span<const MaterialShaderDefinition> builtin_material_shaders() {
        static constexpr std::array definitions{
            MaterialShaderDefinition{"unlit_color", "unlit_color"},
            MaterialShaderDefinition{"pbr", "pbr"}};
        return definitions;
    }

    MaterialShaders default_material_shaders() {
        return {{"unlit_color", {{UNLIT_COLOR_VERT.begin(), UNLIT_COLOR_VERT.end()},
                                    {UNLIT_COLOR_FRAG.begin(), UNLIT_COLOR_FRAG.end()}}},
            {"pbr", {{PBR_VERT.begin(), PBR_VERT.end()}, {PBR_FRAG.begin(), PBR_FRAG.end()}}}};
    }

    Result<void> validate_material_shaders(const MaterialShaders& shaders) {
        if(shaders.empty())
            return Result<void>::failure("Empty material Shader override");
        struct Contract {
            Result<ShaderInterface> vertex;
            Result<ShaderInterface> fragment;
        };
        // 固定契约只来自构建内嵌程序，不随开发覆盖更新。
        static const auto contracts = [] {
            std::map<std::string, Contract, std::less<>> result;
            for(const auto& [name, code] : default_material_shaders())
                result.emplace(name, Contract{ShaderInterface::reflect(code.vertex),
                                         ShaderInterface::reflect(code.fragment)});
            return result;
        }();
        for(const auto& [name, code] : shaders) {
            const auto contract = contracts.find(name);
            if(contract == contracts.end())
                return Result<void>::failure("Unknown material Shader program: " + name);
            if(code.vertex.empty() || code.fragment.empty())
                return Result<void>::failure("Incomplete vertex/fragment pair: " + name);
            const auto validate = [&](std::span<const uint32_t> words,
                                      const Result<ShaderInterface>& expected,
                                      std::optional<uint32_t> ignored_set) {
                if(!expected)
                    return Result<void>::failure(expected.error());
                const auto candidate = ShaderInterface::reflect(words);
                if(!candidate)
                    return Result<void>::failure(candidate.error());
                if(!expected.value().has_same_resource_layout(candidate.value(), ignored_set))
                    return Result<void>::failure("Shader changed fixed resource layout: " + name);
                return Result<void>::success();
            };
            if(auto result = validate(code.vertex, contract->second.vertex, std::nullopt); !result)
                return result;
            if(auto result = validate(code.fragment, contract->second.fragment, 1); !result)
                return result;
        }
        return Result<void>::success();
    }

    void merge_material_shaders(MaterialShaders& destination, MaterialShaders updates) {
        for(auto& [name, program] : updates)
            destination.insert_or_assign(name, std::move(program));
    }
}
