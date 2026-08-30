#pragma once

#include "asset/handle.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "scene/scene.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace Comet {
    enum class PropertyType {
        Bool,
        Float,
        Vec3,
        AssetHandle
    };

    using PropertyValue = std::variant<
        bool,
        float,
        Math::Vec3,
        AssetHandle>;

    [[nodiscard]] inline bool property_values_equal(
        const PropertyValue& left,
        const PropertyValue& right) {
        if(left.index() != right.index()) {
            return false;
        }
        return std::visit([](const auto& left_value, const auto& right_value) {
            using Left = std::remove_cvref_t<decltype(left_value)>;
            using Right = std::remove_cvref_t<decltype(right_value)>;
            if constexpr(!std::is_same_v<Left, Right>) {
                return false;
            } else if constexpr(std::is_same_v<Left, Math::Vec3>) {
                return left_value.x == right_value.x
                    && left_value.y == right_value.y
                    && left_value.z == right_value.z;
            } else {
                return left_value == right_value;
            }
        }, left, right);
    }

    struct NumericPropertyMetadata {
        float speed = 0.1f;
        std::optional<float> minimum;
        std::optional<float> maximum;
    };

    struct PropertyMetadata {
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        NumericPropertyMetadata numeric;
    };

    struct PropertyDescriptor {
        std::string id;
        std::string display_name;
        PropertyType type = PropertyType::Float;
        bool editable = true;
        bool serializable = true;
        bool transient = false;
        bool read_only = false;
        NumericPropertyMetadata numeric;
        std::function<void*(void*)> mutable_accessor;
        std::function<const void*(const void*)> const_accessor;
        std::function<void(void*)> on_changed;
        std::function<std::optional<PropertyValue>(const void*)>
            copy_value_callback;
        std::function<bool(void*, const PropertyValue&)>
            assign_value_callback;

        [[nodiscard]] void* get_value(void* component) const {
            return component != nullptr && mutable_accessor
                ? mutable_accessor(component)
                : nullptr;
        }

        [[nodiscard]] const void* get_value(const void* component) const {
            return component != nullptr && const_accessor
                ? const_accessor(component)
                : nullptr;
        }

        void notify_changed(void* value) const {
            if(value != nullptr && on_changed) {
                on_changed(value);
            }
        }

        [[nodiscard]] std::optional<PropertyValue> copy_value(
            const void* component) const {
            return component != nullptr && copy_value_callback
                ? copy_value_callback(component)
                : std::nullopt;
        }

        [[nodiscard]] bool assign_value(
            void* component,
            const PropertyValue& value) const {
            if(component == nullptr
               || !assign_value_callback
               || !assign_value_callback(component, value)) {
                return false;
            }
            notify_changed(get_value(component));
            return true;
        }
    };

    struct ComponentDescriptor {
        std::string id;
        std::string display_name;
        bool serializable = true;
        std::vector<PropertyDescriptor> properties;
        std::function<bool(const Entity&)> has_component_callback;
        std::function<void(Entity&)> add_component_callback;
        std::function<void(Entity&)> remove_component_callback;
        std::function<void*(Entity&)> mutable_component_accessor;
        std::function<const void*(const Entity&)> const_component_accessor;

        [[nodiscard]] bool has_component(const Entity& entity) const {
            return has_component_callback && has_component_callback(entity);
        }

        [[nodiscard]] bool add_component(Entity& entity) const {
            if(!entity || has_component(entity) || !add_component_callback) {
                return false;
            }
            add_component_callback(entity);
            return has_component(entity);
        }

        [[nodiscard]] bool remove_component(Entity& entity) const {
            if(!entity || !has_component(entity) || !remove_component_callback) {
                return false;
            }
            remove_component_callback(entity);
            return !has_component(entity);
        }

        [[nodiscard]] void* get_component(Entity& entity) const {
            return has_component(entity) && mutable_component_accessor
                ? mutable_component_accessor(entity)
                : nullptr;
        }

        [[nodiscard]] const void* get_component(const Entity& entity) const {
            return has_component(entity) && const_component_accessor
                ? const_component_accessor(entity)
                : nullptr;
        }

        [[nodiscard]] const PropertyDescriptor* find_property(
            const std::string_view property_id) const {
            for(const PropertyDescriptor& property: properties) {
                if(property.id == property_id) {
                    return &property;
                }
            }
            return nullptr;
        }
    };

    class COMET_API ComponentRegistry {
    public:
        [[nodiscard]] bool register_component(ComponentDescriptor descriptor);

        [[nodiscard]] const ComponentDescriptor* find_component(
            std::string_view component_id) const;

        [[nodiscard]] const std::vector<ComponentDescriptor>& components() const {
            return m_components;
        }

    private:
        std::vector<ComponentDescriptor> m_components;
    };

    template<typename Component, typename Value>
    PropertyDescriptor make_property_descriptor(
        std::string id,
        std::string display_name,
        Value Component::* member,
        PropertyMetadata metadata = {}) {
        using StoredValue = std::remove_cvref_t<Value>;
        static_assert(
            std::is_same_v<StoredValue, bool>
            || std::is_same_v<StoredValue, float>
            || std::is_same_v<StoredValue, Math::Vec3>
            || std::is_same_v<StoredValue, AssetHandle>,
            "Unsupported property type");

        constexpr PropertyType type = [] {
            if constexpr(std::is_same_v<StoredValue, bool>) {
                return PropertyType::Bool;
            } else if constexpr(std::is_same_v<StoredValue, float>) {
                return PropertyType::Float;
            } else if constexpr(std::is_same_v<StoredValue, Math::Vec3>) {
                return PropertyType::Vec3;
            } else {
                return PropertyType::AssetHandle;
            }
        }();

        return {
            .id = std::move(id),
            .display_name = std::move(display_name),
            .type = type,
            .editable = metadata.editable,
            .serializable = metadata.serializable,
            .transient = metadata.transient,
            .read_only = metadata.read_only,
            .numeric = std::move(metadata.numeric),
            .mutable_accessor = [member](void* component) -> void* {
                return &(static_cast<Component*>(component)->*member);
            },
            .const_accessor = [member](const void* component) -> const void* {
                return &(static_cast<const Component*>(component)->*member);
            },
            .copy_value_callback = [member](const void* component)
                -> std::optional<PropertyValue> {
                if(component == nullptr) {
                    return std::nullopt;
                }
                return PropertyValue(
                    static_cast<const Component*>(component)->*member);
            },
            .assign_value_callback = [member](
                void* component,
                const PropertyValue& value) {
                if(component == nullptr) {
                    return false;
                }
                const auto stored = std::get_if<StoredValue>(&value);
                if(stored == nullptr) {
                    return false;
                }
                static_cast<Component*>(component)->*member = *stored;
                return true;
            }
        };
    }

    template<typename Component, typename Value, typename Callback>
    PropertyDescriptor make_property_descriptor(
        std::string id,
        std::string display_name,
        Value Component::* member,
        PropertyMetadata metadata,
        Callback&& on_changed) {
        PropertyDescriptor descriptor = make_property_descriptor(
            std::move(id),
            std::move(display_name),
            member,
            std::move(metadata));
        descriptor.on_changed = [callback = std::forward<Callback>(on_changed)](
            void* value) mutable {
            callback(*static_cast<Value*>(value));
        };
        return descriptor;
    }

    template<typename Component>
    ComponentDescriptor make_component_descriptor(
        std::string id,
        std::string display_name,
        std::vector<PropertyDescriptor> properties,
        const bool serializable = true) {
        return {
            .id = std::move(id),
            .display_name = std::move(display_name),
            .serializable = serializable,
            .properties = std::move(properties),
            .has_component_callback = [](const Entity& entity) {
                return entity.has_component<Component>();
            },
            .add_component_callback = [](Entity& entity) {
                entity.add_component<Component>();
            },
            .remove_component_callback = [](const Entity& entity) {
                entity.remove_component<Component>();
            },
            .mutable_component_accessor = [](Entity& entity) -> void* {
                return &entity.get_component<Component>();
            },
            .const_component_accessor = [](const Entity& entity) -> const void* {
                return &entity.get_component<Component>();
            }
        };
    }

    [[nodiscard]] COMET_API ComponentRegistry create_scene_component_registry();
}
