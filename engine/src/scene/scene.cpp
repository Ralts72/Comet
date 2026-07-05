#include "scene/scene.h"
#include <algorithm>
#include <stdexcept>

namespace Comet {
    Entity Scene::create_entity(const std::string& name) {
        const entt::entity handle = m_registry.create();
        m_registry.emplace<IDComponent>(handle, m_next_entity_id++);
        m_registry.emplace<NameComponent>(handle, name);
        m_registry.emplace<TransformComponent>(handle);
        m_registry.emplace<RelationshipComponent>(handle);
        return make_entity(handle);
    }

    void Scene::destroy_entity(const Entity entity) {
        if(!entity.is_valid()) {
            return;
        }

        const entt::entity handle = entity.get_handle();
        remove_from_parent(handle);

        if(auto* relationship = m_registry.try_get<RelationshipComponent>(handle)) {
            for(const entt::entity child: relationship->children) {
                if(m_registry.valid(child)) {
                    m_registry.get<RelationshipComponent>(child).parent = entt::null;
                }
            }
            relationship->children.clear();
        }

        m_registry.destroy(handle);
    }

    Entity Scene::find_entity_by_id(const EntityId id) {
        const auto view = m_registry.view<IDComponent>();
        for(const entt::entity handle: view) {
            if(view.get<IDComponent>(handle).id == id) {
                return make_entity(handle);
            }
        }
        return {};
    }

    void Scene::set_parent(const Entity child, const Entity parent) {
        if(!child.is_valid() || !parent.is_valid()) {
            throw std::runtime_error("Cannot set parent for invalid entity");
        }
        if(child == parent) {
            throw std::runtime_error("Entity cannot be parented to itself");
        }
        if(would_create_cycle(child.get_handle(), parent.get_handle())) {
            throw std::runtime_error("Entity hierarchy cycle is not allowed");
        }

        remove_from_parent(child.get_handle());

        auto& child_relationship = child.get_component<RelationshipComponent>();
        auto& parent_relationship = parent.get_component<RelationshipComponent>();
        child_relationship.parent = parent.get_handle();
        parent_relationship.children.push_back(child.get_handle());
    }

    void Scene::clear_parent(const Entity child) {
        if(!child.is_valid()) {
            return;
        }
        remove_from_parent(child.get_handle());
    }

    Entity Scene::get_parent(const Entity child) {
        if(!child.is_valid()) {
            return {};
        }

        const auto& relationship = child.get_component<RelationshipComponent>();
        if(relationship.parent == entt::null || !m_registry.valid(relationship.parent)) {
            return {};
        }
        return make_entity(relationship.parent);
    }

    std::vector<Entity> Scene::get_children(const Entity parent) {
        std::vector<Entity> children;
        if(!parent.is_valid()) {
            return children;
        }

        const auto& relationship = parent.get_component<RelationshipComponent>();
        children.reserve(relationship.children.size());
        for(const entt::entity child: relationship.children) {
            if(m_registry.valid(child)) {
                children.push_back(make_entity(child));
            }
        }
        return children;
    }

    void Scene::update_transforms() {
        const auto view = m_registry.view<TransformComponent, RelationshipComponent>();
        for(const entt::entity handle: view) {
            const auto& relationship = view.get<RelationshipComponent>(handle);
            if(relationship.parent == entt::null || !m_registry.valid(relationship.parent)) {
                update_transform_recursive(handle, Math::Mat4(1.0f));
            }
        }
    }

    Entity Scene::make_entity(const entt::entity handle) {
        if(handle == entt::null || !m_registry.valid(handle)) {
            return {};
        }
        return Entity(handle, this, &m_registry);
    }

    bool Scene::would_create_cycle(const entt::entity child, entt::entity parent) const {
        while(parent != entt::null) {
            if(parent == child) {
                return true;
            }
            const auto* relationship = m_registry.try_get<RelationshipComponent>(parent);
            if(!relationship) {
                return false;
            }
            parent = relationship->parent;
        }
        return false;
    }

    void Scene::remove_from_parent(const entt::entity child) {
        if(!m_registry.valid(child)) {
            return;
        }

        auto& child_relationship = m_registry.get<RelationshipComponent>(child);
        const entt::entity parent = child_relationship.parent;
        if(parent == entt::null || !m_registry.valid(parent)) {
            child_relationship.parent = entt::null;
            return;
        }

        auto& siblings = m_registry.get<RelationshipComponent>(parent).children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), child), siblings.end());
        child_relationship.parent = entt::null;
    }

    void Scene::update_transform_recursive(const entt::entity entity, const Math::Mat4& parent_world) {
        auto& transform = m_registry.get<TransformComponent>(entity);
        transform.world_matrix = parent_world * transform.get_local_matrix();

        const auto& relationship = m_registry.get<RelationshipComponent>(entity);
        for(const entt::entity child: relationship.children) {
            if(m_registry.valid(child)) {
                update_transform_recursive(child, transform.world_matrix);
            }
        }
    }
}
