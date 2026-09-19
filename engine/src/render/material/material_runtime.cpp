#include "render/material/material_runtime.h"
#include "render/material/material_layout.h"

#include "render/material/material.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace Comet {
    Result<std::shared_ptr<const PreparedMaterial>> MaterialRuntimeCache::prepare(
        const AssetHandle handle, const std::shared_ptr<const Material>& material,
        const std::shared_ptr<const MaterialLayout>& layout) {
        using Preparation = Result<std::shared_ptr<const PreparedMaterial>>;
        if(!material || !layout)
            return Preparation::failure("Material preparation requires a source and layout");
        auto& entry = m_entries[handle];
        entry.used = true;
        if(entry.source == material && entry.layout == layout
            && entry.material_revision == material->get_revision()) {
            if(!entry.error.empty())
                return Preparation::failure(entry.error);
            return Preparation::success(entry.prepared);
        }
        entry.source = material;
        entry.layout = layout;
        entry.material_revision = material->get_revision();
        entry.prepared.reset();
        entry.error.clear();
        if(material->get_template_name() != layout->get_name()) {
            entry.error = "Material requires layout '" + material->get_template_name()
                          + "', renderer provided '" + layout->get_name() + "'";
            return Preparation::failure(entry.error);
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
            if(!texture && !property.optional) {
                entry.error = "Missing texture property '" + property.name + "'";
                return Preparation::failure(entry.error);
            }
            prepared->textures.push_back({property.binding, std::move(texture)});
        }
        std::ranges::sort(prepared->textures, {}, &PreparedMaterial::TextureBinding::binding);
        entry.prepared = std::move(prepared);
        return Preparation::success(entry.prepared);
    }

    Result<std::shared_ptr<const PreparedMaterial>> MaterialRuntimeCache::rebind(
        AssetHandle handle, const std::shared_ptr<const MaterialLayout>& layout) {
        const auto found = m_entries.find(handle);
        if(found == m_entries.end())
            return Result<std::shared_ptr<const PreparedMaterial>>::failure(
                "Resident material source is missing");
        const auto source = found->second.source;
        const bool used = found->second.used;
        auto result = prepare(handle, source, layout);
        found->second.used = used;
        return result;
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

    void MaterialRuntimeCache::merge(MaterialRuntimeCache&& candidates) {
        for(auto& [handle, entry] : candidates.m_entries)
            m_entries.insert_or_assign(handle, std::move(entry));
    }
}
