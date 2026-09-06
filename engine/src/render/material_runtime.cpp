#include "render/material_runtime.h"

#include "diagnostics/logger.h"
#include "render/material.h"
#include "graphics/pipeline/shader_interface.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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
            if(binding.type == vk::DescriptorType::eCombinedImageSampler) {
                if(!binding.image || !binding.image->is_float_2d())
                    fail(
                        "material textures require single-sample floating-point sampler2D");
                if(std::ranges::find(
                       m_textures, binding.binding, &TextureProperty::binding)
                    == m_textures.end()) {
                    fail("missing texture binding " + std::to_string(binding.binding));
                }
                ++texture_count;
                continue;
            }
            if(binding.binding != m_parameter_binding
                || binding.type != vk::DescriptorType::eUniformBuffer
                || m_parameter_size == 0 || binding.block_size != m_parameter_size) {
                fail("parameter block type, binding or size mismatch");
            }
            parameter_block_found = true;
            if(binding.members.size() != m_scalars.size() + m_vectors.size())
                fail("parameter member count mismatch");
            const auto check_member = [&](uint32_t offset, vk::Format format,
                                          const std::string& name) {
                const auto found = std::ranges::find(
                    binding.members, offset, &ShaderInterface::BlockMember::offset);
                if(found == binding.members.end() || found->format != format)
                    fail("parameter '" + name + "' offset or type mismatch");
            };
            for(const auto& scalar : m_scalars)
                check_member(scalar.offset, vk::Format::eR32Sfloat, scalar.name);
            for(const auto& vector : m_vectors)
                check_member(vector.offset, vk::Format::eR32G32B32A32Sfloat, vector.name);
        }
        if(texture_count != m_textures.size())
            fail("texture properties do not match shader bindings");
        if(parameter_block_found != (m_parameter_size > 0))
            fail("parameter block is missing from shader");
    }

    std::shared_ptr<const MaterialLayout> MaterialLayout::find_builtin(
        const std::string_view name) {
        static const std::array<std::shared_ptr<const MaterialLayout>, 4> layouts{
            std::make_shared<MaterialLayout>("cube_texture", 2,
                std::vector<TextureProperty>{{"u_Texture0", 1, "Texture 0", "texture0"},
                    {"u_Texture1", 2, "Texture 1", "texture1"}},
                32,
                std::vector<ScalarProperty>{{"blend", 16, 0.5f, 0, 1, 0.01f, "Blend"}},
                std::vector<VectorProperty>{
                    {"tint", 0, {1, 1, 1, 1}, VectorProperty::Semantic::Color, "Tint"}}),
            std::make_shared<MaterialLayout>("unlit_color", 1,
                std::vector<TextureProperty>{}, 32,
                std::vector<ScalarProperty>{
                    {"intensity", 16, 1.0f, 0, 10, 0.05f, "Intensity"}},
                std::vector<VectorProperty>{{"color", 0, {1, 1, 1, 1},
                    VectorProperty::Semantic::Color, "Color"}}),
            std::make_shared<MaterialLayout>("lit_color", 1,
                std::vector<TextureProperty>{}, 16, std::vector<ScalarProperty>{},
                std::vector<VectorProperty>{{"albedo", 0, {0.8f, 0.8f, 0.8f, 1},
                    VectorProperty::Semantic::Color, "Albedo"}}),
            std::make_shared<MaterialLayout>("pbr_color", 1,
                std::vector<TextureProperty>{}, 32,
                std::vector<ScalarProperty>{{"metallic", 16, 0, 0, 1, 0.01f, "Metallic"},
                    {"roughness", 20, 0.5f, 0.045f, 1, 0.01f, "Roughness"}},
                std::vector<VectorProperty>{{"base_color", 0, {0.8f, 0.8f, 0.8f, 1},
                    VectorProperty::Semantic::Color, "Base color"}})};
        const auto found = std::ranges::find_if(
            layouts, [&](const auto& layout) { return layout->get_name() == name; });
        if(found == layouts.end())
            return nullptr;
        return *found;
    }

    MaterialLayout::MaterialLayout(std::string name, const uint64_t revision,
        std::vector<TextureProperty> textures, const uint32_t parameter_size,
        std::vector<ScalarProperty> scalars, std::vector<VectorProperty> vectors,
        const uint32_t parameter_binding)
        : m_name(std::move(name)), m_revision(revision), m_textures(std::move(textures)),
          m_parameter_size(parameter_size), m_parameter_binding(parameter_binding),
          m_scalars(std::move(scalars)), m_vectors(std::move(vectors)) {
        if(m_name.empty() || m_revision == 0) {
            throw std::invalid_argument("Material layout requires a name and revision");
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
            || (parameter_size > 0 && bindings.contains(parameter_binding))) {
            throw std::invalid_argument(
                "Material parameters require a bounded std140 block and unique binding");
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
            if(!std::ranges::all_of(vector.default_value,
                   [](float value) { return std::isfinite(value); })) {
                throw std::invalid_argument("Material default must be finite");
            }
        }
    }

    std::shared_ptr<const PreparedMaterial> MaterialRuntimeCache::prepare(
        const AssetHandle handle, const std::shared_ptr<const Material>& material,
        const std::shared_ptr<const MaterialLayout>& layout) {
        if(!material || !layout) {
            return nullptr;
        }
        auto& entry = m_entries[handle];
        entry.used = true;
        if(entry.source == material && entry.layout == layout
            && entry.material_revision == material->get_revision()) {
            return entry.prepared;
        }
        entry.source = material;
        entry.layout = layout;
        entry.material_revision = material->get_revision();
        entry.prepared.reset();
        if(material->get_template_name() != layout->get_name()) {
            LOG_ERROR("Material handle {} requires layout '{}', renderer provided '{}'",
                handle.value(), material->get_template_name(), layout->get_name());
            return nullptr;
        }
        auto prepared = std::make_shared<PreparedMaterial>();
        prepared->layout = layout;
        prepared->parameters.resize(layout->get_parameter_size());
        for(const auto& property : layout->get_scalars()) {
            const float value = material->get_scalar_property(property.name)
                                    .value_or(property.default_value);
            std::memcpy(
                prepared->parameters.data() + property.offset, &value, sizeof(value));
        }
        for(const auto& property : layout->get_vectors()) {
            const auto value = material->get_vector_property(property.name)
                                   .value_or(property.default_value);
            std::memcpy(prepared->parameters.data() + property.offset, value.data(),
                sizeof(value));
        }
        prepared->textures.reserve(layout->get_textures().size());
        for(const auto& property : layout->get_textures()) {
            auto texture = material->get_texture_property(property.name);
            if(!texture) {
                LOG_ERROR("Material handle {} is missing texture property '{}'",
                    handle.value(), property.name);
                return nullptr;
            }
            prepared->textures.push_back({property.binding, std::move(texture)});
        }
        entry.prepared = std::move(prepared);
        return entry.prepared;
    }

    std::shared_ptr<const MaterialLayout> MaterialLayout::reflect(
        const std::shared_ptr<const MaterialLayout>& metadata,
        const ShaderInterface& shader) {
        if(!metadata)
            throw std::invalid_argument("Material reflection requires semantic metadata");
        auto textures = metadata->m_textures;
        auto scalars = metadata->m_scalars;
        auto vectors = metadata->m_vectors;
        uint32_t size = 0;
        uint32_t parameter_binding = 0;
        std::vector<const ShaderInterface::DescriptorBinding*> texture_bindings;
        const ShaderInterface::DescriptorBinding* parameters = nullptr;
        for(const auto& binding : shader.get_bindings()) {
            if(binding.set != 1)
                continue;
            if(binding.count != 1)
                throw std::invalid_argument(
                    "Material descriptor arrays require explicit metadata support");
            if(binding.type == vk::DescriptorType::eCombinedImageSampler)
                texture_bindings.push_back(&binding);
            else if(binding.type == vk::DescriptorType::eUniformBuffer && !parameters)
                parameters = &binding;
            else
                throw std::invalid_argument("Unsupported material resource shape");
        }
        if(texture_bindings.size() != textures.size())
            throw std::invalid_argument(
                "Shader textures do not match registered material metadata");
        const auto shader_name = [](const auto& property) -> const std::string& {
            if(!property.shader_name.empty())
                return property.shader_name;
            return property.name;
        };
        bool changed = false;
        std::unordered_set<uint32_t> matched_textures;
        for(auto& property : textures) {
            const auto found =
                std::ranges::find_if(texture_bindings, [&](const auto* binding) {
                    return binding->name == shader_name(property);
                });
            if(found == texture_bindings.end()
                || !matched_textures.insert((*found)->binding).second)
                throw std::invalid_argument(
                    "Missing or repeated Shader texture for material property: "
                    + property.name);
            changed |= property.binding != (*found)->binding;
            property.binding = (*found)->binding;
        }
        if(parameters) {
            size = parameters->block_size;
            parameter_binding = parameters->binding;
            if(parameters->members.size() != scalars.size() + vectors.size())
                throw std::invalid_argument(
                    "Shader parameters do not match registered material metadata");
            std::unordered_set<uint32_t> matched_offsets;
            const auto assign = [&](auto& properties, vk::Format format) {
                for(auto& property : properties) {
                    const auto found = std::ranges::find(parameters->members,
                        shader_name(property), &ShaderInterface::BlockMember::name);
                    if(found == parameters->members.end() || found->format != format
                        || !matched_offsets.insert(found->offset).second)
                        throw std::invalid_argument(
                            "Missing, repeated or incompatible Shader parameter: "
                            + property.name);
                    changed |= property.offset != found->offset;
                    property.offset = found->offset;
                }
            };
            assign(scalars, vk::Format::eR32Sfloat);
            assign(vectors, vk::Format::eR32G32B32A32Sfloat);
        } else if(!scalars.empty() || !vectors.empty()) {
            throw std::invalid_argument("Material parameter block is missing");
        }
        changed |= size != metadata->m_parameter_size
                   || parameter_binding != metadata->m_parameter_binding;
        if(!changed) {
            metadata->validate(shader);
            return metadata;
        }
        if(metadata->m_revision == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Material layout revision exhausted");
        auto result = std::make_shared<MaterialLayout>(metadata->m_name,
            metadata->m_revision + 1, std::move(textures), size, std::move(scalars),
            std::move(vectors), parameter_binding);
        result->validate(shader);
        return result;
    }

    std::shared_ptr<const PreparedMaterial> MaterialRuntimeCache::rebind(
        const AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout) {
        const auto found = m_entries.find(handle);
        if(found == m_entries.end())
            return nullptr;
        const auto source = found->second.source;
        return prepare(handle, source, layout);
    }

    void MaterialRuntimeCache::swap(MaterialRuntimeCache& other) noexcept {
        m_entries.swap(other.m_entries);
    }

    void MaterialRuntimeCache::collect_unused() {
        std::erase_if(m_entries, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, entry] : m_entries) {
            entry.used = false;
        }
    }
}
