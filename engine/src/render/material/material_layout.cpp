#include "render/material/material_layout.h"
#include "diagnostics/logger.h"
#include "graphics/pipeline/shader_interface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace Comet {
    namespace {
        template<typename Property> const std::string& shader_name(const Property& property) {
            return property.shader_name.empty() ? property.name : property.shader_name;
        }
    }

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
                if(!binding.sampled_image || !binding.sampled_image->is_float_2d())
                    return fail("texture properties require single-sampled float sampler2D");
                if(std::ranges::find(m_textures, binding.binding, &TextureProperty::binding)
                    == m_textures.end()) {
                    return fail("missing texture binding " + std::to_string(binding.binding));
                }
                ++texture_count;
                continue;
            }
            if(binding.binding != m_parameter_binding
                || binding.type != DescriptorType::UniformBuffer || m_parameter_size == 0
                || binding.block_size != m_parameter_size) {
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

    std::span<const std::shared_ptr<const MaterialLayout>> MaterialLayout::builtins() {
        const auto builtin =
            [](Result<MaterialLayout> candidate) -> std::shared_ptr<const MaterialLayout> {
            if(!candidate)
                LOG_FATAL("Invalid built-in material layout: {}", candidate.error());
            return std::make_shared<MaterialLayout>(std::move(candidate).value());
        };
        static const std::array<std::shared_ptr<const MaterialLayout>, 2> layouts{
            builtin(create("unlit_color", std::vector<TextureProperty>{}, 32,
                std::vector<ScalarProperty>{{"intensity", 16, 1.0f, 0, 10, 0.05f, "Intensity"}},
                std::vector<VectorProperty>{
                    {"color", 0, {1, 1, 1, 1}, VectorProperty::Semantic::Color, "Color"}})),
            builtin(create("pbr",
                std::vector<TextureProperty>{
                    {"base_color_texture", 1, "Base Color Texture", "", true}},
                32,
                std::vector<ScalarProperty>{{"metallic", 16, 0, 0, 1, 0.01f, "Metallic"},
                    {"roughness", 20, 0.5f, 0.045f, 1, 0.01f, "Roughness"}},
                std::vector<VectorProperty>{{"base_color", 0, {0.8f, 0.8f, 0.8f, 1},
                    VectorProperty::Semantic::Color, "Base Color"}}))};
        return layouts;
    }

    std::shared_ptr<const MaterialLayout> MaterialLayout::find_builtin(
        const std::string_view name) {
        const auto layouts = builtins();
        const auto found = std::ranges::find_if(
            layouts, [&](const auto& layout) { return layout->get_name() == name; });
        if(found == layouts.end())
            return nullptr;
        return *found;
    }

    Result<MaterialLayout> MaterialLayout::create(std::string name,
        std::vector<TextureProperty> textures, const uint32_t parameter_size,
        std::vector<ScalarProperty> scalars, std::vector<VectorProperty> vectors,
        const uint32_t parameter_binding) {
        MaterialLayout candidate;
        candidate.m_name = std::move(name);
        candidate.m_textures = std::move(textures);
        candidate.m_parameter_size = parameter_size;
        candidate.m_parameter_binding = parameter_size ? parameter_binding : 0;
        candidate.m_scalars = std::move(scalars);
        candidate.m_vectors = std::move(vectors);
        if(candidate.m_name.empty()) {
            return Result<MaterialLayout>::failure("Material layout requires a name");
        }
        std::unordered_set<std::string> names;
        std::unordered_set<uint32_t> bindings;
        std::unordered_set<std::string> texture_names;
        for(const auto& texture : candidate.m_textures) {
            if(texture.name.empty() || !names.insert(texture.name).second
                || !texture_names.insert(shader_name(texture)).second
                || !bindings.insert(texture.binding).second) {
                return Result<MaterialLayout>::failure("Material layout has invalid texture slots");
            }
        }
        if(parameter_size % 16 != 0 || parameter_size > 65536
            || (parameter_size > 0 && bindings.contains(parameter_binding))) {
            return Result<MaterialLayout>::failure(
                "Material parameters require a bounded std140 block and unique binding");
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
        std::unordered_set<std::string> parameter_names;
        for(const auto& scalar : candidate.m_scalars) {
            if(!parameter_names.insert(shader_name(scalar)).second)
                return Result<MaterialLayout>::failure("Duplicate material shader property name");
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
            if(!parameter_names.insert(shader_name(vector)).second)
                return Result<MaterialLayout>::failure("Duplicate material shader property name");
            if(auto checked = validate_parameter(vector.name, vector.offset, 4 * sizeof(float), 16);
                !checked)
                return Result<MaterialLayout>::failure(checked.error());
            if(!Math::is_finite(vector.default_value)) {
                return Result<MaterialLayout>::failure("Material default must be finite");
            }
        }
        return Result<MaterialLayout>::success(std::move(candidate));
    }

    Result<std::shared_ptr<const MaterialLayout>> MaterialLayout::reflect(
        const std::shared_ptr<const MaterialLayout>& metadata, const ShaderInterface& shader) {
        using Layout = Result<std::shared_ptr<const MaterialLayout>>;
        if(!metadata || shader.get_stage() != ShaderStage::Fragment)
            return Layout::failure("Material reflection requires metadata and a fragment shader");
        auto textures = metadata->m_textures;
        auto scalars = metadata->m_scalars;
        auto vectors = metadata->m_vectors;
        uint32_t parameter_size = 0;
        uint32_t parameter_binding = 0;
        size_t texture_count = 0;
        bool parameter_block = false;
        bool changed = false;
        std::unordered_set<std::string> matched_textures;
        for(const auto& binding : shader.get_bindings()) {
            if(binding.set != 1)
                continue;
            if(binding.count != 1)
                return Layout::failure("Material descriptor arrays are not supported");
            if(binding.type == DescriptorType::CombinedImageSampler) {
                auto property = std::ranges::find_if(
                    textures, [&](const auto& item) { return shader_name(item) == binding.name; });
                if(property == textures.end() || !matched_textures.insert(binding.name).second)
                    return Layout::failure("Unknown material texture: " + binding.name);
                if(!binding.sampled_image || !binding.sampled_image->is_float_2d())
                    return Layout::failure("Material textures require float sampler2D");
                changed |= property->binding != binding.binding;
                property->binding = binding.binding;
                ++texture_count;
                continue;
            }
            if(binding.type != DescriptorType::UniformBuffer || parameter_block
                || binding.members.size() != scalars.size() + vectors.size()
                || binding.members.empty())
                return Layout::failure("Unsupported material parameter block");
            parameter_block = true;
            parameter_size = binding.block_size;
            parameter_binding = binding.binding;
            const auto update = [&](auto& properties, Format format) -> Result<void> {
                for(auto& property : properties) {
                    const auto member = std::ranges::find(binding.members, shader_name(property),
                        &ShaderInterface::BlockMember::name);
                    if(member == binding.members.end() || member->format != format)
                        return Result<void>::failure(
                            "Missing or incompatible material parameter: " + shader_name(property));
                    changed |= property.offset != member->offset;
                    property.offset = member->offset;
                }
                return Result<void>::success();
            };
            if(auto checked = update(scalars, Format::R32_SFLOAT); !checked)
                return Layout::failure(checked.error());
            if(auto checked = update(vectors, Format::R32G32B32A32_SFLOAT); !checked)
                return Layout::failure(checked.error());
        }
        if(texture_count != textures.size()
            || parameter_block != (!scalars.empty() || !vectors.empty()))
            return Layout::failure("Shader is missing registered material properties");
        changed |= parameter_size != metadata->m_parameter_size
                   || parameter_binding != metadata->m_parameter_binding;
        if(!changed) {
            if(auto checked = metadata->validate(shader); !checked)
                return Layout::failure(checked.error());
            return Layout::success(metadata);
        }
        auto candidate = create(metadata->m_name, std::move(textures), parameter_size,
            std::move(scalars), std::move(vectors), parameter_binding);
        if(!candidate)
            return Layout::failure(candidate.error());
        if(auto checked = candidate.value().validate(shader); !checked)
            return Layout::failure(checked.error());
        return Layout::success(std::make_shared<MaterialLayout>(std::move(candidate).value()));
    }

}
