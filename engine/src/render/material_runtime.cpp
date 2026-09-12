#include "render/material_runtime.h"

#include "diagnostics/logger.h"
#include "render/material.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    MaterialLayout::MaterialLayout(std::string name, const uint64_t revision,
        std::vector<TextureProperty> textures, const uint32_t parameter_size,
        std::vector<ScalarProperty> scalars, std::vector<VectorProperty> vectors)
        : m_name(std::move(name)), m_revision(revision), m_textures(std::move(textures)),
          m_parameter_size(parameter_size), m_scalars(std::move(scalars)),
          m_vectors(std::move(vectors)) {
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
        }
        for(const auto& vector : m_vectors) {
            validate_parameter(vector.name, vector.offset, 4 * sizeof(float), 16);
            if(!Math::is_finite(vector.default_value)) {
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
            for(int component = 0; component < 4; ++component) {
                std::memcpy(prepared->parameters.data() + property.offset
                                + component * sizeof(float),
                    &value[component], sizeof(float));
            }
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

    void MaterialRuntimeCache::collect_unused() {
        std::erase_if(m_entries, [](const auto& entry) { return !entry.second.used; });
        for(auto& [handle, entry] : m_entries) {
            entry.used = false;
        }
    }
}
