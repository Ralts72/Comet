#pragma once

#include "common/export.h"
#include "common/result.h"
#include "core/math_utils.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    class ShaderInterface;

    class COMET_API MaterialLayout {
    public:
        struct TextureProperty {
            std::string name;
            uint32_t binding;
            std::string display_name;
            std::string shader_name;
            // 未指定的可选纹理由渲染端绑定白色纹理。
            bool optional = false;
        };
        struct ScalarProperty {
            std::string name;
            uint32_t offset;
            float default_value;
            float min_value = 0;
            float max_value = 0;
            float step = 0.01f;
            std::string display_name;
            std::string shader_name;
        };
        struct VectorProperty {
            enum class Semantic { Vector, Color };
            std::string name;
            uint32_t offset;
            Math::Vec4 default_value{0.0f};
            Semantic semantic = Semantic::Vector;
            std::string display_name;
            std::string shader_name;
        };

        [[nodiscard]] static std::shared_ptr<const MaterialLayout> find_builtin(
            std::string_view name);

        static Result<MaterialLayout> create(std::string name,
            std::vector<TextureProperty> textures, uint32_t parameter_size = 0,
            std::vector<ScalarProperty> scalars = {}, std::vector<VectorProperty> vectors = {},
            uint32_t parameter_binding = 0);
        static Result<std::shared_ptr<const MaterialLayout>> reflect(
            const std::shared_ptr<const MaterialLayout>& metadata, const ShaderInterface& shader);
        MaterialLayout(const MaterialLayout&) = default;
        MaterialLayout(MaterialLayout&&) = default;
        MaterialLayout& operator=(const MaterialLayout&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] const std::vector<TextureProperty>& get_textures() const {
            return m_textures;
        }
        [[nodiscard]] uint32_t get_parameter_size() const { return m_parameter_size; }
        [[nodiscard]] uint32_t get_parameter_binding() const { return m_parameter_binding; }
        [[nodiscard]] const std::vector<ScalarProperty>& get_scalars() const { return m_scalars; }
        [[nodiscard]] const std::vector<VectorProperty>& get_vectors() const { return m_vectors; }

        Result<void> validate(const ShaderInterface& shader, uint32_t material_set = 1) const;

    private:
        MaterialLayout() = default;
        std::string m_name;
        std::vector<TextureProperty> m_textures;
        uint32_t m_parameter_size;
        uint32_t m_parameter_binding = 0;
        std::vector<ScalarProperty> m_scalars;
        std::vector<VectorProperty> m_vectors;
    };

}
