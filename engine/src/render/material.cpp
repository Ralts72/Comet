#include "render/material.h"
#include "diagnostics/logger.h"
#include "graphics/pipeline/shader_interface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    Result<void> MaterialLayout::validate(
        const ShaderInterface& shader, const uint32_t material_set) const {
        const auto fail = [&](const std::string& reason) {
            return Result<void>::failure("Material layout '" + m_name + "': " + reason);
        };
        size_t texture_count = 0;
        bool parameter_block_found = false;
        for(const auto& binding : shader.get_bindings()) {
            if(binding.set != material_set)
                continue;
            if(binding.count != 1)
                return fail("descriptor arrays are not material properties");
            if(binding.type == DescriptorType::CombinedImageSampler) {
                if(std::ranges::find(m_textures, binding.binding, &TextureProperty::binding)
                    == m_textures.end()) {
                    return fail("missing texture binding " + std::to_string(binding.binding));
                }
                ++texture_count;
                continue;
            }
            if(binding.binding != 0 || binding.type != DescriptorType::UniformBuffer
                || m_parameter_size == 0 || binding.block_size != m_parameter_size) {
                return fail("parameter block type, binding or size mismatch");
            }
            parameter_block_found = true;
            if(binding.members.size() != m_scalars.size() + m_vectors.size())
                return fail("parameter member count mismatch");
            const auto check_member = [&](uint32_t offset, Format format, const std::string& name) {
                const auto found = std::ranges::find(
                    binding.members, offset, &ShaderInterface::BlockMember::offset);
                if(found == binding.members.end() || found->format != format)
                    return fail("parameter '" + name + "' offset or type mismatch");
                return Result<void>::success();
            };
            for(const auto& scalar : m_scalars) {
                if(auto checked = check_member(scalar.offset, Format::R32_SFLOAT, scalar.name);
                    !checked)
                    return checked;
            }
            for(const auto& vector : m_vectors) {
                if(auto checked =
                        check_member(vector.offset, Format::R32G32B32A32_SFLOAT, vector.name);
                    !checked)
                    return checked;
            }
        }
        if(texture_count != m_textures.size())
            return fail("texture properties do not match shader bindings");
        if(parameter_block_found != (m_parameter_size > 0))
            return fail("parameter block is missing from shader");
        return Result<void>::success();
    }

    std::shared_ptr<const MaterialLayout> MaterialLayout::find_builtin(
        const std::string_view name) {
        const auto builtin =
            [](Result<MaterialLayout> candidate) -> std::shared_ptr<const MaterialLayout> {
            if(!candidate)
                LOG_FATAL("Invalid built-in material layout: {}", candidate.error());
            return std::make_shared<MaterialLayout>(std::move(candidate).value());
        };
        static const std::array<std::shared_ptr<const MaterialLayout>, 2> layouts{
            builtin(create("unlit_texture_blend",
                std::vector<TextureProperty>{
                    {"u_Texture0", 1, "Texture 0"}, {"u_Texture1", 2, "Texture 1"}},
                32, std::vector<ScalarProperty>{{"blend", 16, 0.5f, 0, 1, 0.01f, "Blend"}},
                std::vector<VectorProperty>{
                    {"tint", 0, {1, 1, 1, 1}, VectorProperty::Semantic::Color, "Tint"}})),
            builtin(create("unlit_color", std::vector<TextureProperty>{}, 32,
                std::vector<ScalarProperty>{{"intensity", 16, 1.0f, 0, 10, 0.05f, "Intensity"}},
                std::vector<VectorProperty>{
                    {"color", 0, {1, 1, 1, 1}, VectorProperty::Semantic::Color, "Color"}}))};
        const auto found = std::ranges::find_if(
            layouts, [&](const auto& layout) { return layout->get_name() == name; });
        if(found == layouts.end())
            return nullptr;
        return *found;
    }

    Result<MaterialLayout> MaterialLayout::create(std::string name,
        std::vector<TextureProperty> textures, const uint32_t parameter_size,
        std::vector<ScalarProperty> scalars, std::vector<VectorProperty> vectors) {
        MaterialLayout candidate;
        candidate.m_name = std::move(name);
        candidate.m_textures = std::move(textures);
        candidate.m_parameter_size = parameter_size;
        candidate.m_scalars = std::move(scalars);
        candidate.m_vectors = std::move(vectors);
        if(candidate.m_name.empty()) {
            return Result<MaterialLayout>::failure("Material layout requires a name");
        }
        std::unordered_set<std::string> names;
        std::unordered_set<uint32_t> bindings;
        for(const auto& texture : candidate.m_textures) {
            if(texture.name.empty() || !names.insert(texture.name).second
                || !bindings.insert(texture.binding).second) {
                return Result<MaterialLayout>::failure("Material layout has invalid texture slots");
            }
        }
        std::ranges::sort(candidate.m_textures, {}, &TextureProperty::binding);
        if(parameter_size % 16 != 0 || parameter_size > 65536
            || (parameter_size > 0 && bindings.contains(0))) {
            return Result<MaterialLayout>::failure(
                "Material parameters require a bounded std140 block at binding 0");
        }
        std::vector<bool> occupied(parameter_size);
        const auto validate_parameter = [&](const std::string& property_name, uint32_t offset,
                                            uint32_t size, uint32_t alignment) {
            if(property_name.empty() || !names.insert(property_name).second
                || offset % alignment != 0 || offset > parameter_size
                || size > parameter_size - offset) {
                return Result<void>::failure("Invalid material parameter range or name");
            }
            for(uint32_t byte = offset; byte < offset + size; ++byte) {
                if(occupied[byte])
                    return Result<void>::failure("Overlapping material parameters");
                occupied[byte] = true;
            }
            return Result<void>::success();
        };
        for(const auto& scalar : candidate.m_scalars) {
            if(auto checked = validate_parameter(scalar.name, scalar.offset, sizeof(float), 4);
                !checked)
                return Result<MaterialLayout>::failure(checked.error());
            if(!std::isfinite(scalar.default_value)) {
                return Result<MaterialLayout>::failure("Material default must be finite");
            }
            if(!std::isfinite(scalar.min_value) || !std::isfinite(scalar.max_value)
                || scalar.min_value > scalar.max_value || !std::isfinite(scalar.step)
                || scalar.step <= 0) {
                return Result<MaterialLayout>::failure("Invalid material scalar editing metadata");
            }
        }
        for(const auto& vector : candidate.m_vectors) {
            if(auto checked = validate_parameter(vector.name, vector.offset, 4 * sizeof(float), 16);
                !checked)
                return Result<MaterialLayout>::failure(checked.error());
            if(!Math::is_finite(vector.default_value)) {
                return Result<MaterialLayout>::failure("Material default must be finite");
            }
        }
        return Result<MaterialLayout>::success(std::move(candidate));
    }

    Material::Material(std::string name, std::string template_name)
        : m_name(std::move(name)), m_template_name(std::move(template_name)) {}

    void Material::set_texture_property(const std::string& name, std::shared_ptr<Texture> texture) {
        const auto found = m_texture_properties.find(name);
        if(found != m_texture_properties.end() && found->second == texture) {
            return;
        }
        m_texture_properties[name] = std::move(texture);
        ++m_revision;
    }

    std::shared_ptr<Texture> Material::get_texture_property(const std::string& name) const {
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

    std::optional<Math::Vec4> Material::get_vector_property(const std::string& name) const {
        const auto found = m_vector_properties.find(name);
        if(found == m_vector_properties.end())
            return std::nullopt;
        return found->second;
    }
}
