#include "scene/component_registry.h"

#include "scene/components.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    bool ComponentRegistry::register_component(ComponentDescriptor descriptor) {
        if(descriptor.id.empty()
           || descriptor.display_name.empty()
           || !descriptor.has_component_callback
           || !descriptor.add_component_callback
           || !descriptor.remove_component_callback
           || !descriptor.mutable_component_accessor
           || !descriptor.const_component_accessor
           || find_component(descriptor.id) != nullptr) {
            return false;
        }

        std::unordered_set<std::string> property_ids;
        for(const PropertyDescriptor& property: descriptor.properties) {
            if(property.id.empty()
               || property.display_name.empty()
               || !property.mutable_accessor
               || !property.const_accessor
               || (property.transient && property.serializable)
               || !property_ids.insert(property.id).second) {
                return false;
            }
        }

        m_components.push_back(std::move(descriptor));
        return true;
    }

    const ComponentDescriptor* ComponentRegistry::find_component(
        const std::string_view component_id) const {
        for(const ComponentDescriptor& component: m_components) {
            if(component.id == component_id) {
                return &component;
            }
        }
        return nullptr;
    }

    ComponentRegistry create_scene_component_registry() {
        ComponentRegistry registry;
        const auto register_component = [&registry](ComponentDescriptor descriptor) {
            if(!registry.register_component(std::move(descriptor))) {
                throw std::logic_error("Invalid built-in component descriptor");
            }
        };

        register_component(make_component_descriptor<TransformComponent>(
            "transform",
            "Transform",
            {
                make_property_descriptor(
                    "translation",
                    "Translation",
                    &TransformComponent::translation),
                make_property_descriptor(
                    "rotation",
                    "Rotation",
                    &TransformComponent::rotation,
                    {.numeric = {.speed = 1.0f}},
                    [](Math::Vec3& rotation) {
                        rotation = Math::wrap_degrees(rotation);
                    }),
                make_property_descriptor(
                    "scale",
                    "Scale",
                    &TransformComponent::scale)
            }));

        register_component(make_component_descriptor<MeshRendererComponent>(
            "mesh_renderer",
            "Mesh Renderer",
            {
                make_property_descriptor(
                    "mesh", "Mesh", &MeshRendererComponent::mesh),
                make_property_descriptor(
                    "material", "Material", &MeshRendererComponent::material)
            }));

        register_component(make_component_descriptor<CameraComponent>(
            "camera",
            "Camera",
            {
                make_property_descriptor(
                    "primary", "Primary", &CameraComponent::primary),
                make_property_descriptor(
                    "fov",
                    "Field of View",
                    &CameraComponent::fov,
                    {.numeric = {
                        .speed = 1.0f,
                        .minimum = 1.0f,
                        .maximum = 179.0f
                    }}),
                make_property_descriptor(
                    "near_clip",
                    "Near Clip",
                    &CameraComponent::near_clip,
                    {.numeric = {.speed = 0.01f, .minimum = 0.001f}}),
                make_property_descriptor(
                    "far_clip",
                    "Far Clip",
                    &CameraComponent::far_clip,
                    {.numeric = {.speed = 1.0f, .minimum = 0.001f}})
            }));

        return registry;
    }
}
