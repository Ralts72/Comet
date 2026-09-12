#pragma once

#include "common/export.h"
#include "core/math_utils.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Comet {
    class Texture;
    class ShaderInterface;

    class COMET_API MaterialLayout {
    public:
        struct TextureProperty {
            std::string name;
            uint32_t binding;
            std::string display_name;
        };
        struct ScalarProperty {
            std::string name;
            uint32_t offset;
            float default_value;
            float min_value = 0;
            float max_value = 0;
            float step = 0.01f;
            std::string display_name;
        };
        struct VectorProperty {
            enum class Semantic { Vector, Color };
            std::string name;
            uint32_t offset;
            Math::Vec4 default_value{0.0f};
            Semantic semantic = Semantic::Vector;
            std::string display_name;
        };

        [[nodiscard]] static std::shared_ptr<const MaterialLayout> find_builtin(
            std::string_view name);

        MaterialLayout(std::string name, std::vector<TextureProperty> textures,
            uint32_t parameter_size = 0, std::vector<ScalarProperty> scalars = {},
            std::vector<VectorProperty> vectors = {});
        MaterialLayout(const MaterialLayout&) = default;
        MaterialLayout& operator=(const MaterialLayout&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] const std::vector<TextureProperty>& get_textures() const {
            return m_textures;
        }
        [[nodiscard]] uint32_t get_parameter_size() const { return m_parameter_size; }
        [[nodiscard]] const std::vector<ScalarProperty>& get_scalars() const {
            return m_scalars;
        }
        [[nodiscard]] const std::vector<VectorProperty>& get_vectors() const {
            return m_vectors;
        }

        void validate(const ShaderInterface& shader, uint32_t material_set = 1) const;

    private:
        std::string m_name;
        std::vector<TextureProperty> m_textures;
        uint32_t m_parameter_size;
        std::vector<ScalarProperty> m_scalars;
        std::vector<VectorProperty> m_vectors;
    };

    class COMET_API Material {
    public:
        Material(std::string name, std::string template_name);
        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

        [[nodiscard]] const std::string& get_name() const { return m_name; }
        [[nodiscard]] uint64_t get_revision() const { return m_revision; }
        [[nodiscard]] const std::string& get_template_name() const {
            return m_template_name;
        }

        void set_texture_property(
            const std::string& name, std::shared_ptr<Texture> texture);
        void set_scalar_property(const std::string& name, float value);
        void set_vector_property(const std::string& name, Math::Vec4 value);
        [[nodiscard]] std::optional<float> get_scalar_property(
            const std::string& name) const;
        [[nodiscard]] std::optional<Math::Vec4> get_vector_property(
            const std::string& name) const;

        [[nodiscard]] std::shared_ptr<Texture> get_texture_property(
            const std::string& name) const;

    private:
        std::string m_name;
        std::string m_template_name;
        uint64_t m_revision = 1;
        std::map<std::string, std::shared_ptr<Texture>> m_texture_properties;
        std::map<std::string, float> m_scalar_properties;
        std::map<std::string, Math::Vec4> m_vector_properties;
    };
}
