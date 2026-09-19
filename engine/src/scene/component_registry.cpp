#include "scene/component_registry.h"

#include "scene/components.h"

#include "diagnostics/logger.h"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace Comet {
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
            || !descriptor.has_component_callback || !descriptor.mutable_component_accessor
            || !descriptor.const_component_accessor || find_component(descriptor.id) != nullptr) {
            return false;
        }

        std::unordered_set<std::string> property_ids;
        for(const PropertyDescriptor& property : descriptor.properties) {
            if(property.id.empty() || property.display_name.empty() || !property.mutable_accessor
                || !property.const_accessor || (property.transient && property.serializable)
                || (property.asset_type
                    && (property.type != PropertyType::AssetHandle
                        || *property.asset_type == AssetType::Unknown))
                || !property_ids.insert(property.id).second) {
                return false;
            }
            if(property.type == PropertyType::Enum) {
                if(property.enum_options.empty() || !property.read_enum || !property.write_enum)
                    return false;
                std::unordered_set<std::string> names;
                for(const auto& option : property.enum_options)
                    if(option.id.empty() || option.display_name.empty()
                        || !names.insert(option.id).second)
                        return false;
            } else if(!property.enum_options.empty() || property.read_enum || property.write_enum) {
                return false;
            }
        }

        m_components.push_back(std::move(descriptor));
        return true;
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
                || type == entt::type_hash<WorldTransformComponent>::value())
                continue;
            bool registered = false;
            for(const auto& component : m_components)
                registered |= component.type_id == type;
            if(!registered)
                return false;
        }
        return true;
    }

    std::vector<AssetReference> ComponentRegistry::collect_asset_references(Scene& scene) const {
        std::vector<AssetReference> references;
        for(const auto entity : scene.get_entities()) {
            for(const auto& component : m_components) {
                const auto* data = component.get_component(entity);
                if(!data)
                    continue;
                for(const auto& property : component.properties) {
                    if(property.type != PropertyType::AssetHandle || !property.asset_type)
                        continue;
                    if(const auto value = property.copy_value(data)) {
                        if(const auto handle = std::get<AssetHandle>(*value))
                            references.push_back({handle, *property.asset_type});
                    }
                }
            }
        }
        std::ranges::sort(references);
        references.erase(std::unique(references.begin(), references.end()), references.end());
        return references;
    }

    ComponentRegistry create_scene_component_registry() {
        ComponentRegistry registry;
        const auto register_component = [&registry](ComponentDescriptor descriptor) {
            if(!registry.register_component(std::move(descriptor))) {
                LOG_FATAL("Invalid built-in component descriptor");
            }
        };

        // 名称已由 .scene 单独保存，避免重复序列化。
        register_component(make_component_descriptor<NameComponent>("name", "Name",
            {make_property_descriptor("name", "Name", &NameComponent::name)}, false));

        register_component(make_component_descriptor<TransformComponent>("transform", "Transform",
            {make_property_descriptor(
                 "translation", "Translation", &TransformComponent::translation),
                make_property_descriptor("rotation", "Rotation", &TransformComponent::rotation,
                    {.numeric = {.speed = 1.0f}},
                    [](Math::Vec3& rotation) { rotation = Math::wrap_degrees(rotation); }),
                make_property_descriptor("scale", "Scale", &TransformComponent::scale)}));

        register_component(
            make_component_descriptor<MeshRendererComponent>("mesh_renderer", "Mesh Renderer",
                {make_property_descriptor(
                     "mesh", "Mesh", &MeshRendererComponent::mesh, {.asset_type = AssetType::Mesh}),
                    make_property_descriptor("material", "Material",
                        &MeshRendererComponent::material, {.asset_type = AssetType::Material})}));

        register_component(make_component_descriptor<CameraComponent>("camera", "Camera",
            {make_property_descriptor("primary", "Primary", &CameraComponent::primary),
                make_property_descriptor("fov", "Field of View", &CameraComponent::fov,
                    {.numeric = {.speed = 1.0f, .minimum = 1.0f, .maximum = 179.0f}}),
                make_property_descriptor("near_clip", "Near Clip", &CameraComponent::near_clip,
                    {.numeric = {.speed = 0.01f, .minimum = 0.001f}}),
                make_property_descriptor("far_clip", "Far Clip", &CameraComponent::far_clip,
                    {.numeric = {.speed = 1.0f, .minimum = 0.001f}})}));

        register_component(make_component_descriptor<LightComponent>("light", "Light",
            {make_enum_property_descriptor<LightComponent, LightType>("type", "Type",
                 &LightComponent::type,
                 {{LightType::Directional, {"directional", "Directional"}},
                     {LightType::Point, {"point", "Point"}}, {LightType::Spot, {"spot", "Spot"}}}),
                make_property_descriptor("enabled", "Enabled", &LightComponent::enabled),
                make_property_descriptor("color", "Color (linear)", &LightComponent::color,
                    {.numeric = {.speed = 0.01f, .minimum = 0.0f, .maximum = 1.0f}}),
                make_property_descriptor("intensity", "Intensity", &LightComponent::intensity,
                    {.numeric = {.speed = 0.1f, .minimum = 0.0f, .maximum = 10000.0f}}),
                make_property_descriptor("range", "Range", &LightComponent::range,
                    {.numeric = {.speed = 0.1f, .minimum = 0.001f, .maximum = 1000000.0f}}),
                make_property_descriptor("inner_angle", "Inner angle", &LightComponent::inner_angle,
                    {.numeric = {.speed = 0.5f, .minimum = 0.0f, .maximum = 89.0f}}),
                make_property_descriptor("outer_angle", "Outer angle", &LightComponent::outer_angle,
                    {.numeric = {.speed = 0.5f, .minimum = 0.01f, .maximum = 89.0f}})}));

        return registry;
    }
}
