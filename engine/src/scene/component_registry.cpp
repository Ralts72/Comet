#include "scene/component_registry.h"
#include "scene/prefab.h"

#include "scene/components.h"

#include "diagnostics/logger.h"
#include <algorithm>
#include <utility>

namespace Comet {
    namespace {
        bool references_assets(const PropertyDescriptor& property) {
            return property.asset_type || std::ranges::any_of(property.members, references_assets)
                   || (property.element && references_assets(*property.element));
        }
    }

    bool ComponentDescriptor::assign_property(Entity entity, std::string_view property_id,
        const PropertyValue& value, PropertyDescriptor::WriteMode mode) const {
        const auto property = resolve_property(property_id);
        if(!has_component(entity) || !property || !assign_property_callback)
            return false;
        return assign_property_callback(entity, *property, value, mode);
    }

    std::any ComponentDescriptor::capture_component(const Entity& entity) const {
        if(!entity || !has_component(entity) || !capture_component_callback)
            return {};
        return capture_component_callback(entity);
    }

    bool ComponentDescriptor::restore_component(Entity& entity, const std::any& snapshot) const {
        if(!entity || has_component(entity) || !snapshot.has_value() || !restore_component_callback)
            return false;
        return restore_component_callback(entity, snapshot);
    }

    bool ComponentRegistry::register_component(ComponentDescriptor descriptor) {
        if(descriptor.id.empty() || descriptor.display_name.empty()
            || !descriptor.has_component_callback || !descriptor.assign_property_callback
            || !descriptor.const_component_accessor || find_component(descriptor.id) != nullptr) {
            return false;
        }

        for(const auto& existing : m_components)
            if(descriptor.type_id != 0 && existing.type_id == descriptor.type_id)
                return false;

        if(!descriptor.valid())
            return false;

        m_components.push_back(std::move(descriptor));
        return true;
    }

    void ComponentRegistry::set_reference_filter(
        std::string_view id, std::function<bool(const void*, ReferenceScope)> filter) {
        const auto found = std::ranges::find(m_components, id, &ComponentDescriptor::id);
        if(found == m_components.end())
            LOG_FATAL("Unknown component reference filter {}", id);
        found->include_references = std::move(filter);
    }

    const ComponentDescriptor* ComponentRegistry::find_component(
        const std::string_view component_id) const {
        for(const ComponentDescriptor& component : m_components) {
            if(component.id == component_id) {
                return &component;
            }
        }
        return nullptr;
    }

    bool ComponentRegistry::covers_entity(const Entity& entity) const {
        if(!entity)
            return false;
        for(auto&& [type, storage] : entity.m_scene->m_registry.storage()) {
            if(!storage.contains(entity.m_handle))
                continue;
            if(type == entt::type_hash<IdComponent>::value()
                || type == entt::type_hash<UuidComponent>::value()
                || type == entt::type_hash<NameComponent>::value()
                || type == entt::type_hash<RelationshipComponent>::value()
                || type == entt::type_hash<WorldTransformComponent>::value()
                || type == entt::type_hash<PrefabInstanceComponent>::value())
                continue;
            bool registered = false;
            for(const auto& component : m_components)
                registered |= component.type_id == type;
            if(!registered)
                return false;
        }
        return true;
    }

    std::vector<AssetReference> ComponentRegistry::collect_asset_references(
        Scene& scene, const ReferenceScope scope) const {
        std::vector<AssetReference> references;
        for(const auto& collect : m_scene_references)
            collect(scene, scope, references);
        for(const auto entity : scene.get_entities()) {
            for(const auto& component : m_components) {
                const auto* data = component.get_component(entity);
                if(!data
                    || (component.include_references && !component.include_references(data, scope)))
                    continue;
                for(const auto& property : component.properties) {
                    if(!references_assets(property))
                        continue;
                    if(const auto value = property.copy_value(data))
                        property.collect_asset_references(*value, references);
                }
            }
        }
        std::ranges::sort(references);
        references.erase(std::unique(references.begin(), references.end()), references.end());
        return references;
    }

}
