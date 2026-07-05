#pragma once
#include "common/export.h"
#include "scene/entity.h"
#include <entt.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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

        [[nodiscard]] Entity find_entity_by_id(EntityId id);

        void set_parent(Entity child, Entity parent);

        void clear_parent(Entity child);

        [[nodiscard]] Entity get_parent(Entity child);

        [[nodiscard]] std::vector<Entity> get_children(Entity parent);

        void update_transforms();

        template<typename T, typename... Args>
        T& add_component(Entity entity, Args&&... args);

        template<typename T>
        [[nodiscard]] bool has_component(Entity entity) const;

        template<typename T>
        T& get_component(Entity entity);

        template<typename T>
        void remove_component(Entity entity);

        [[nodiscard]] entt::registry& get_registry() { return m_registry; }
        [[nodiscard]] const entt::registry& get_registry() const { return m_registry; }

    private:
        friend class Entity;

        [[nodiscard]] Entity make_entity(entt::entity handle);
        [[nodiscard]] bool owns_entity(Entity entity) const;
        [[nodiscard]] bool would_create_cycle(entt::entity child, entt::entity parent) const;

        void ensure_entity_valid(Entity entity) const;
        void remove_from_parent(entt::entity child);
        void update_transform_recursive(entt::entity entity, const Math::Mat4& parent_world);

        entt::registry m_registry;
        EntityId m_next_entity_id = 1;
    };

    template<typename T, typename... Args>
    T& Scene::add_component(const Entity entity, Args&&... args) {
        ensure_entity_valid(entity);
        if(has_component<T>(entity)) {
            throw std::runtime_error("Entity already has requested component");
        }
        return m_registry.emplace<T>(entity.m_handle, std::forward<Args>(args)...);
    }

    template<typename T>
    bool Scene::has_component(const Entity entity) const {
        return owns_entity(entity) && m_registry.all_of<T>(entity.m_handle);
    }

    template<typename T>
    T& Scene::get_component(const Entity entity) {
        ensure_entity_valid(entity);
        if(!has_component<T>(entity)) {
            throw std::runtime_error("Entity does not have requested component");
        }
        return m_registry.get<T>(entity.m_handle);
    }

    template<typename T>
    void Scene::remove_component(const Entity entity) {
        ensure_entity_valid(entity);
        if(!has_component<T>(entity)) {
            throw std::runtime_error("Entity does not have requested component");
        }
        m_registry.remove<T>(entity.m_handle);
    }

    template<typename T, typename... Args>
    T& Entity::add_component(Args&&... args) {
        if(!m_scene) {
            throw std::runtime_error("Entity handle is invalid");
        }
        return m_scene->add_component<T>(*this, std::forward<Args>(args)...);
    }

    template<typename T>
    bool Entity::has_component() const {
        return m_scene && m_scene->has_component<T>(*this);
    }

    template<typename T>
    T& Entity::get_component() const {
        if(!m_scene) {
            throw std::runtime_error("Entity handle is invalid");
        }
        return m_scene->get_component<T>(*this);
    }

    template<typename T>
    void Entity::remove_component() {
        if(!m_scene) {
            throw std::runtime_error("Entity handle is invalid");
        }
        m_scene->remove_component<T>(*this);
    }
}
