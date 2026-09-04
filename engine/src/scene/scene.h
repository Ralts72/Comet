#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/export.h"
#include "scene/entity.h"
#include <entt.hpp>

namespace Comet {
    class SceneExtractor;
    class SceneSerializer;

    class COMET_API Scene {
    public:
        Scene() = default;

        ~Scene() = default;

        Scene(const Scene&) = delete;

        Scene& operator=(const Scene&) = delete;

        Scene(Scene&&) noexcept = delete;

        Scene& operator=(Scene&&) noexcept = delete;

        Entity create_entity(const std::string& name = "Entity");

        [[nodiscard]] Entity create_entity_with_uuid(
            EntityUuid uuid, const std::string& name = "Entity");

        void destroy_entity(Entity entity);

        [[nodiscard]] bool set_parent(Entity child, Entity parent);

        [[nodiscard]] bool clear_parent(Entity child);

        [[nodiscard]] Entity get_parent(Entity entity);

        [[nodiscard]] std::vector<Entity> get_children(Entity entity);

        [[nodiscard]] std::vector<Entity> get_root_entities();

        void update_world_transforms();

        [[nodiscard]] const Math::Mat4& get_world_matrix(Entity entity);

        [[nodiscard]] Entity find_entity(EntityId id);

        [[nodiscard]] Entity find_entity(EntityUuid uuid);

        [[nodiscard]] std::vector<Entity> get_entities();

        [[nodiscard]] bool is_valid(Entity entity) const;

        [[nodiscard]] std::size_t entity_count() const;

    private:
        friend class Entity;
        friend class SceneExtractor;
        friend class SceneSerializer;

        [[nodiscard]] bool has_cycle(Entity child, Entity parent);

        EntityId m_next_entity_id = 1;
        entt::registry m_registry;
    };

    template<typename T, typename... Args>
        requires(!is_scene_managed_component_v<T>)
    T& Entity::add_component(Args&&... args) {
        return m_scene->m_registry.emplace<T>(m_handle, std::forward<Args>(args)...);
    }

    template<typename T>
        requires(!is_scene_read_only_component_v<T>)
    T& Entity::get_component() {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T> const T& Entity::get_component() const {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T> bool Entity::has_component() const {
        return static_cast<bool>(*this) && m_scene->m_registry.all_of<T>(m_handle);
    }

    template<typename T>
        requires(!is_scene_managed_component_v<T>)
    void Entity::remove_component() const {
        m_scene->m_registry.remove<T>(m_handle);
    }
}
