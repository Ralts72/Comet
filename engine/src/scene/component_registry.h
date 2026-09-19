#pragma once

#include "asset/reference.h"
#include "scene/property.h"
#include "common/export.h"
#include "scene/scene.h"

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Comet {
    struct ComponentDescriptor {
        std::string id;
        std::string display_name;
        entt::id_type type_id = 0; // 进程内类型标识，不写入场景文件。
        bool serializable = true;
        std::vector<PropertyDescriptor> properties;
        std::function<bool(const Entity&)> has_component_callback;
        std::function<void(Entity&)> add_component_callback;
        std::function<void(Entity&)> remove_component_callback;
        std::function<void*(Entity&)> mutable_component_accessor;
        std::function<const void*(const Entity&)> const_component_accessor;
        std::function<std::any(const Entity&)> capture_component_callback;
        std::function<bool(Entity&, const std::any&)> restore_component_callback;

        [[nodiscard]] COMET_API std::any capture_component(const Entity& entity) const;
        [[nodiscard]] COMET_API bool restore_component(
            Entity& entity, const std::any& snapshot) const;

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
            if(!has_component(entity) || !mutable_component_accessor) {
                return nullptr;
            }
            return mutable_component_accessor(entity);
        }

        [[nodiscard]] const void* get_component(const Entity& entity) const {
            if(!has_component(entity) || !const_component_accessor) {
                return nullptr;
            }
            return const_component_accessor(entity);
        }

        [[nodiscard]] const PropertyDescriptor* find_property(
            const std::string_view property_id) const {
            for(const PropertyDescriptor& property : properties) {
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
        [[nodiscard]] bool covers_entity(const Entity& entity) const;
        [[nodiscard]] std::vector<AssetReference> collect_asset_references(Scene& scene) const;

        [[nodiscard]] const std::vector<ComponentDescriptor>& components() const {
            return m_components;
        }

    private:
        std::vector<ComponentDescriptor> m_components;
    };

    template<typename Component>
    ComponentDescriptor make_component_descriptor(std::string id, std::string display_name,
        std::vector<PropertyDescriptor> properties, const bool serializable = true) {
        ComponentDescriptor descriptor{.id = std::move(id),
            .display_name = std::move(display_name),
            .type_id = entt::type_hash<Component>::value(),
            .serializable = serializable,
            .properties = std::move(properties),
            .has_component_callback =
                [](const Entity& entity) { return entity.has_component<Component>(); },
            .mutable_component_accessor = [](Entity& entity) -> void* {
                return &entity.get_component<Component>();
            },
            .const_component_accessor = [](const Entity& entity) -> const void* {
                return &entity.get_component<Component>();
            }};
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

    [[nodiscard]] COMET_API ComponentRegistry create_scene_component_registry();
}
