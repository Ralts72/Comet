#include "render/material.h"
#include "graphics/pipeline/shader_interface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    void MaterialLayout::validate(
        const ShaderInterface& shader, const uint32_t material_set) const {
        const auto fail = [&](const std::string& reason) {
            throw std::invalid_argument("Material layout '" + m_name + "': " + reason);
        };
        size_t texture_count = 0;
        bool parameter_block_found = false;
        for(const auto& binding : shader.get_bindings()) {
            if(binding.set != material_set)
                continue;
            if(binding.count != 1)
                fail("descriptor arrays are not material properties");
            if(binding.type == DescriptorType::CombinedImageSampler) {
                if(std::ranges::find(
                       m_textures, binding.binding, &TextureProperty::binding)
                    == m_textures.end()) {
                    fail("missing texture binding " + std::to_string(binding.binding));
                }
                ++texture_count;
                continue;
            }
            if(binding.binding != 0 || binding.type != DescriptorType::UniformBuffer
                || m_parameter_size == 0 || binding.block_size != m_parameter_size) {
                fail("parameter block type, binding or size mismatch");
            }
            parameter_block_found = true;
            if(binding.members.size() != m_scalars.size() + m_vectors.size())
                fail("parameter member count mismatch");
            const auto check_member = [&](uint32_t offset, Format format,
                                          const std::string& name) {
                const auto found = std::ranges::find(
                    binding.members, offset, &ShaderInterface::BlockMember::offset);
                if(found == binding.members.end() || found->format != format)
                    fail("parameter '" + name + "' offset or type mismatch");
            };
            for(const auto& scalar : m_scalars)
                check_member(scalar.offset, Format::R32_SFLOAT, scalar.name);
            for(const auto& vector : m_vectors)
                check_member(vector.offset, Format::R32G32B32A32_SFLOAT, vector.name);
        }
        if(texture_count != m_textures.size())
            fail("texture properties do not match shader bindings");
        if(parameter_block_found != (m_parameter_size > 0))
            fail("parameter block is missing from shader");
    }

    std::shared_ptr<const MaterialLayout> MaterialLayout::find_builtin(
        const std::string_view name) {
        static const std::array<std::shared_ptr<const MaterialLayout>, 2> layouts{
            std::make_shared<MaterialLayout>("unlit_texture_blend",
                std::vector<TextureProperty>{
                    {"u_Texture0", 1, "Texture 0"}, {"u_Texture1", 2, "Texture 1"}},
                32,
                std::vector<ScalarProperty>{{"blend", 16, 0.5f, 0, 1, 0.01f, "Blend"}},
                std::vector<VectorProperty>{
                    {"tint", 0, {1, 1, 1, 1}, VectorProperty::Semantic::Color, "Tint"}}),
            std::make_shared<MaterialLayout>("unlit_color",
                std::vector<TextureProperty>{}, 32,
                std::vector<ScalarProperty>{
                    {"intensity", 16, 1.0f, 0, 10, 0.05f, "Intensity"}},
                std::vector<VectorProperty>{{"color", 0, {1, 1, 1, 1},
                    VectorProperty::Semantic::Color, "Color"}})};
        const auto found = std::ranges::find_if(
            layouts, [&](const auto& layout) { return layout->get_name() == name; });
        if(found == layouts.end())
            return nullptr;
        return *found;
    }

    MaterialLayout::MaterialLayout(std::string name,
        std::vector<TextureProperty> textures, const uint32_t parameter_size,
        std::vector<ScalarProperty> scalars, std::vector<VectorProperty> vectors)
        : m_name(std::move(name)), m_textures(std::move(textures)),
          m_parameter_size(parameter_size), m_scalars(std::move(scalars)),
          m_vectors(std::move(vectors)) {
        if(m_name.empty()) {
            throw std::invalid_argument("Material layout requires a name");
        }
        std::unordered_set<std::string> names;
        std::unordered_set<uint32_t> bindings;
        for(const auto& texture : m_textures) {
            if(texture.name.empty() || !names.insert(texture.name).second
                || !bindings.insert(texture.binding).second) {
                throw std::invalid_argument("Material layout has invalid texture slots");
            }
        }
        std::ranges::sort(m_textures, {}, &TextureProperty::binding);
        if(parameter_size % 16 != 0 || parameter_size > 65536
            || (parameter_size > 0 && bindings.contains(0))) {
            throw std::invalid_argument(
                "Material parameters require a bounded std140 block at binding 0");
        }
        std::vector<bool> occupied(parameter_size);
        const auto validate_parameter = [&](const std::string& property_name,
                                            uint32_t offset, uint32_t size,
                                            uint32_t alignment) {
            if(property_name.empty() || !names.insert(property_name).second
                || offset % alignment != 0 || offset > parameter_size
                || size > parameter_size - offset) {
                throw std::invalid_argument("Invalid material parameter range or name");
            }
            for(uint32_t byte = offset; byte < offset + size; ++byte) {
                if(occupied[byte])
                    throw std::invalid_argument("Overlapping material parameters");
                occupied[byte] = true;
            }
        };
        for(const auto& scalar : m_scalars) {
            validate_parameter(scalar.name, scalar.offset, sizeof(float), 4);
            if(!std::isfinite(scalar.default_value)) {
                throw std::invalid_argument("Material default must be finite");
            }
            if(!std::isfinite(scalar.min_value) || !std::isfinite(scalar.max_value)
                || scalar.min_value > scalar.max_value || !std::isfinite(scalar.step)
                || scalar.step <= 0) {
                throw std::invalid_argument("Invalid material scalar editing metadata");
            }
        }
        for(const auto& vector : m_vectors) {
            validate_parameter(vector.name, vector.offset, 4 * sizeof(float), 16);
            if(!Math::is_finite(vector.default_value)) {
                throw std::invalid_argument("Material default must be finite");
            }
        }
    }

    Material::Material(std::string name, std::string template_name)
        : m_name(std::move(name)), m_template_name(std::move(template_name)) {}

    void Material::set_texture_property(
        const std::string& name, std::shared_ptr<Texture> texture) {
        const auto found = m_texture_properties.find(name);
        if(found != m_texture_properties.end() && found->second == texture) {
            return;
        }
        m_texture_properties[name] = std::move(texture);
        ++m_revision;
    }

    std::shared_ptr<Texture> Material::get_texture_property(
        const std::string& name) const {
        const auto property = m_texture_properties.find(name);
        return property == m_texture_properties.end() ? nullptr : property->second;
    }

    void Material::set_scalar_property(const std::string& name, const float value) {
        if(!std::isfinite(value)) {
            throw std::invalid_argument("Material scalar must be finite");
        }
        const auto found = m_scalar_properties.find(name);
        if(found != m_scalar_properties.end() && found->second == value)
            return;
        m_scalar_properties[name] = value;
        ++m_revision;
    }

    void Material::set_vector_property(const std::string& name, const Math::Vec4 value) {
        if(!Math::is_finite(value)) {
            throw std::invalid_argument("Material vector must be finite");
        }
        const auto found = m_vector_properties.find(name);
        if(found != m_vector_properties.end() && found->second == value)
            return;
        m_vector_properties[name] = value;
        ++m_revision;
    }

    std::optional<float> Material::get_scalar_property(const std::string& name) const {
        const auto found = m_scalar_properties.find(name);
        if(found == m_scalar_properties.end())
            return std::nullopt;
        return found->second;
    }

    std::optional<Math::Vec4> Material::get_vector_property(
        const std::string& name) const {
        const auto found = m_vector_properties.find(name);
        if(found == m_vector_properties.end())
            return std::nullopt;
        return found->second;
    }
}
