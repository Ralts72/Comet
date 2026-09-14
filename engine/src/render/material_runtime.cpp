#include "render/material_runtime.h"

#include "diagnostics/logger.h"
#include "render/material.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace Comet {
    std::shared_ptr<const PreparedMaterial> MaterialRuntimeCache::prepare(const AssetHandle handle,
        const std::shared_ptr<const Material>& material,
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
            const float value =
                material->get_scalar_property(property.name).value_or(property.default_value);
            std::memcpy(prepared->parameters.data() + property.offset, &value, sizeof(value));
        }
        for(const auto& property : layout->get_vectors()) {
            const auto value =
                material->get_vector_property(property.name).value_or(property.default_value);
            for(int component = 0; component < 4; ++component) {
                std::memcpy(
                    prepared->parameters.data() + property.offset + component * sizeof(float),
                    &value[component], sizeof(float));
            }
        }
        prepared->textures.reserve(layout->get_textures().size());
        for(const auto& property : layout->get_textures()) {
            auto texture = material->get_texture_property(property.name);
            if(!texture) {
                LOG_ERROR("Material handle {} is missing texture property '{}'", handle.value(),
                    property.name);
                return nullptr;
            }
            prepared->textures.push_back({property.binding, std::move(texture)});
        }
        entry.prepared = std::move(prepared);
        return entry.prepared;
    }

    Result<std::shared_ptr<const PreparedMaterial>> MaterialRuntimeCache::rebind(
        AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout) {
        const auto found = m_entries.find(handle);
        if(found == m_entries.end())
            return Result<std::shared_ptr<const PreparedMaterial>>::failure(
                "Resident material source is missing");
        const auto source = found->second.source;
        auto prepared = prepare(handle, source, layout);
        if(!prepared)
            return Result<std::shared_ptr<const PreparedMaterial>>::failure(
                "Cannot prepare resident material " + std::to_string(handle.value()));
        return Result<std::shared_ptr<const PreparedMaterial>>::success(std::move(prepared));
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
