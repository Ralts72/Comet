#include "scene/component_registry.h"

#include "scene/components.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace Comet {
    bool property_values_equal(const PropertyValue& left, const PropertyValue& right) {
        if(left.index() != right.index()) {
            return false;
        }
        return std::visit(
            [&right](const auto& value) {
                using Value = std::remove_cvref_t<decltype(value)>;
                return value == std::get<Value>(right);
            },
            left);
    }

    std::optional<PropertyValue> PropertyDescriptor::copy_value(
        const void* component) const {
        const void* value = get_value(component);
        if(!value) {
            return std::nullopt;
        }
        switch(type) {
            case PropertyType::Bool:
                return *static_cast<const bool*>(value);
            case PropertyType::Float:
                return *static_cast<const float*>(value);
            case PropertyType::Vec3:
                return *static_cast<const Math::Vec3*>(value);
            case PropertyType::AssetHandle:
                return *static_cast<const AssetHandle*>(value);
        }
        return std::nullopt;
    }

    bool PropertyDescriptor::assign_value(
        void* component, const PropertyValue& value) const {
        void* destination = get_value(component);
        if(!destination || !editable || read_only) {
            return false;
        }
        const bool assigned = std::visit(
            [this, destination](const auto& source) {
                using Value = std::remove_cvref_t<decltype(source)>;
                if constexpr(std::is_same_v<Value, bool>) {
                    if(type != PropertyType::Bool)
                        return false;
                } else if constexpr(std::is_same_v<Value, float>) {
                    if(type != PropertyType::Float || !std::isfinite(source))
                        return false;
                } else if constexpr(std::is_same_v<Value, Math::Vec3>) {
                    if(type != PropertyType::Vec3 || !Math::is_finite(source))
                        return false;
                } else {
                    if(type != PropertyType::AssetHandle)
                        return false;
                }
                *static_cast<Value*>(destination) = source;
                return true;
            },
            value);
        if(assigned) {
            notify_changed(destination);
        }
        return assigned;
    }

    bool ComponentRegistry::register_component(ComponentDescriptor descriptor) {
        if(descriptor.id.empty() || descriptor.display_name.empty()
            || !descriptor.has_component_callback || !descriptor.add_component_callback
            || !descriptor.remove_component_callback
            || !descriptor.mutable_component_accessor
            || !descriptor.const_component_accessor
            || find_component(descriptor.id) != nullptr) {
            return false;
        }

        std::unordered_set<std::string> property_ids;
        for(const PropertyDescriptor& property : descriptor.properties) {
            if(property.id.empty() || property.display_name.empty()
                || !property.mutable_accessor || !property.const_accessor
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
        for(const ComponentDescriptor& component : m_components) {
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

        register_component(make_component_descriptor<TransformComponent>("transform",
            "Transform",
            {make_property_descriptor(
                 "translation", "Translation", &TransformComponent::translation),
                make_property_descriptor("rotation", "Rotation",
                    &TransformComponent::rotation, {.numeric = {.speed = 1.0f}},
                    [](Math::Vec3& rotation) {
                        rotation = Math::wrap_degrees(rotation);
                    }),
                make_property_descriptor("scale", "Scale", &TransformComponent::scale)}));

        register_component(make_component_descriptor<MeshRendererComponent>(
            "mesh_renderer", "Mesh Renderer",
            {make_property_descriptor("mesh", "Mesh", &MeshRendererComponent::mesh),
                make_property_descriptor(
                    "material", "Material", &MeshRendererComponent::material)}));

        register_component(make_component_descriptor<CameraComponent>("camera", "Camera",
            {make_property_descriptor("primary", "Primary", &CameraComponent::primary),
                make_property_descriptor("fov", "Field of View", &CameraComponent::fov,
                    {.numeric = {.speed = 1.0f, .minimum = 1.0f, .maximum = 179.0f}}),
                make_property_descriptor("near_clip", "Near Clip",
                    &CameraComponent::near_clip,
                    {.numeric = {.speed = 0.01f, .minimum = 0.001f}}),
                make_property_descriptor("far_clip", "Far Clip",
                    &CameraComponent::far_clip,
                    {.numeric = {.speed = 1.0f, .minimum = 0.001f}})}));

        return registry;
    }
}
