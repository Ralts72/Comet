#pragma once

#include "scene/component_registry.h"
#include "reflection/registration.h"

namespace Comet {
    template<typename Component>
    ComponentDescriptor make_component_descriptor(std::string id, std::string display_name,
        std::vector<PropertyDescriptor> properties, const bool serializable = true) {
        ComponentDescriptor descriptor{.type_id = entt::type_hash<Component>::value(),
            .serializable = serializable,
            .has_component_callback =
                [](const Entity& entity) { return entity.has_component<Component>(); },
            .assign_property_callback =
                [](Entity& entity, const PropertyDescriptor& property, const PropertyValue& value,
                    PropertyDescriptor::WriteMode mode) {
                    if constexpr(std::is_same_v<Component, TransformComponent>) {
                        auto candidate = entity.get_component<Component>();
                        return property.assign_value(&candidate, value, mode)
                               && entity.try_set_transform(candidate);
                    } else if constexpr(is_scene_tracked_component_v<Component>) {
                        auto candidate = entity.get_component<Component>();
                        return property.assign_value(&candidate, value, mode)
                               && entity.try_set_component(candidate);
                    } else {
                        return property.assign_value(
                            &entity.get_component<Component>(), value, mode);
                    }
                },
            .const_component_accessor = [](const Entity& entity) -> const void* {
                return &entity.get_component<Component>();
            }};
        descriptor.id = std::move(id);
        descriptor.display_name = std::move(display_name);
        descriptor.properties = std::move(properties);
        if constexpr(!is_scene_managed_component_v<Component>) {
            descriptor.add_component_callback = [](Entity& entity) {
                entity.add_component<Component>();
            };
            descriptor.remove_component_callback = [](Entity& entity) {
                entity.remove_component<Component>();
            };
            if constexpr(std::is_copy_constructible_v<Component>) {
                descriptor.capture_component_callback = [](const Entity& entity) {
                    return std::any(entity.get_component<Component>());
                };
                descriptor.restore_component_callback = [](Entity& entity,
                                                            const std::any& snapshot) {
                    const auto* value = std::any_cast<Component>(&snapshot);
                    if(!value)
                        return false;
                    entity.add_component<Component>(*value);
                    return true;
                };
            }
        }
        return descriptor;
    }

    template<typename Component>
    auto describe_component(std::string id, std::string display_name, bool serializable = true) {
        return TypeBuilder<Component, ComponentDescriptor>(make_component_descriptor<Component>(
            std::move(id), std::move(display_name), {}, serializable));
    }

}

namespace Comet::ComponentRegistration {
    void add(ComponentRegistry&, ComponentDescriptor);
    void core(ComponentRegistry&);
    void render(ComponentRegistry&);
    void animation(ComponentRegistry&);
    void physics(ComponentRegistry&);
    void gameplay(ComponentRegistry&);
    void audio(ComponentRegistry&);
    void particles(ComponentRegistry&);
}
