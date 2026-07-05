#pragma once
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "common/export.h"
#include "scene/entity.h"
#include <entt.hpp>

namespace Comet {
    class COMET_API Scene {
    public:
        Scene() = default;
        ~Scene() = default;

        Scene(const Scene&) = delete;
        Scene& operator=(const Scene&) = delete;
        Scene(Scene&&) noexcept = delete;
        Scene& operator=(Scene&&) noexcept = delete;

        Entity create_entity(const std::string& name = "Entity");

        void destroy_entity(Entity entity);

        [[nodiscard]] Entity find_entity(EntityId id);

        [[nodiscard]] std::vector<Entity> get_entities();

        [[nodiscard]] bool is_valid(Entity entity) const;

        [[nodiscard]] std::size_t entity_count() const;

    private:
        friend class Entity;

        EntityId m_next_entity_id = 1;
        entt::registry m_registry;
    };

    template<typename T, typename... Args>
    T& Entity::add_component(Args&&... args) {
        return m_scene->m_registry.emplace<T>(m_handle, std::forward<Args>(args)...);
    }

    template<typename T>
    T& Entity::get_component() {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T>
    const T& Entity::get_component() const {
        return m_scene->m_registry.get<T>(m_handle);
    }

    template<typename T>
    bool Entity::has_component() const {
        return static_cast<bool>(*this) && m_scene->m_registry.all_of<T>(m_handle);
    }

    template<typename T>
    void Entity::remove_component() {
        m_scene->m_registry.remove<T>(m_handle);
    }
}
