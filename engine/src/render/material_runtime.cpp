#include "render/material_runtime.h"

#include "diagnostics/logger.h"
#include "render/material.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    MaterialLayout::MaterialLayout(
        std::string name, const uint64_t revision, std::vector<TextureProperty> textures)
        : m_name(std::move(name)), m_revision(revision), m_textures(std::move(textures)) {
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
