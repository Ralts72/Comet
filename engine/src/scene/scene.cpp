#include "scene/scene.h"

#include "common/logger.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Comet {
    Entity Scene::create_entity(const std::string& name) {
        const entt::entity handle = m_registry.create();
        Entity entity(handle, this);

        entity.add_component<IdComponent>(m_next_entity_id++);
        entity.add_component<NameComponent>(name.empty() ? "Entity" : name);
        entity.add_component<TransformComponent>();
        entity.add_component<RelationshipComponent>();
        entity.add_component<WorldTransformComponent>();

        return entity;
    }

    void Scene::destroy_entity(const Entity entity) {
        if(!is_valid(entity)) {
            return;
        }

        std::unordered_set<EntityId> destroying;
        const auto destroy_subtree = [this, &destroying](
            const Entity current,
            const auto& destroy_subtree_ref) -> void {
            if(!is_valid(current) || !destroying.insert(current.get_id()).second) {
                return;
            }

            for(const Entity child: get_children(current)) {
                destroy_subtree_ref(child, destroy_subtree_ref);
            }
            m_registry.destroy(current.m_handle);
        };
        destroy_subtree(entity, destroy_subtree);
    }

    bool Scene::set_parent(const Entity child, const Entity parent) {
        if(!is_valid(child) || !is_valid(parent)
           || child == parent || has_cycle(child, parent)) {
            return false;
        }

        auto& relationship =
            m_registry.get_or_emplace<RelationshipComponent>(child.m_handle);
        if(relationship.parent == parent.get_id()) {
            return true;
        }

        relationship.parent = parent.get_id();
        return true;
    }

    bool Scene::clear_parent(const Entity child) {
        if(!is_valid(child)) {
            return false;
        }

        auto& relationship =
            m_registry.get_or_emplace<RelationshipComponent>(child.m_handle);
        if(relationship.parent == INVALID_ENTITY_ID) {
            return true;
        }

        relationship.parent = INVALID_ENTITY_ID;
        return true;
    }

    Entity Scene::get_parent(const Entity entity) {
        if(!is_valid(entity) || !entity.has_component<RelationshipComponent>()) {
            return {};
        }

        return find_entity(entity.get_component<RelationshipComponent>().parent);
    }

    std::vector<Entity> Scene::get_children(const Entity entity) {
        std::vector<Entity> children;
        if(!is_valid(entity)) {
            return children;
        }

        const EntityId parent_id = entity.get_id();
        const auto view = m_registry.view<IdComponent, RelationshipComponent>();
        for(const entt::entity handle: view) {
            if(view.get<RelationshipComponent>(handle).parent == parent_id) {
                children.push_back(Entity(handle, this));
            }
        }
        std::ranges::sort(children, {}, &Entity::get_id);
        return children;
    }

    std::vector<Entity> Scene::get_root_entities() {
        std::vector<Entity> roots;
        for(const Entity entity: get_entities()) {
            if(!get_parent(entity)) {
                roots.push_back(entity);
            }
        }
        std::ranges::sort(roots, {}, &Entity::get_id);
        return roots;
    }

    bool Scene::has_cycle(const Entity child, const Entity parent) {
        std::unordered_set<EntityId> visited;
        Entity ancestor = parent;
        while(ancestor) {
            if(ancestor == child || !visited.insert(ancestor.get_id()).second) {
                return true;
            }
            ancestor = get_parent(ancestor);
        }
        return false;
    }

    void Scene::update_world_transforms() {
        std::unordered_map<EntityId, entt::entity> handles;
        const auto id_view = m_registry.view<IdComponent>();
        handles.reserve(entity_count());
        for(const entt::entity handle: id_view) {
            handles.emplace(id_view.get<IdComponent>(handle).id, handle);
        }

        std::unordered_map<EntityId, std::vector<entt::entity>> children;
        std::vector<entt::entity> roots;
        roots.reserve(handles.size());
        for(const auto& [id, handle]: handles) {
            const auto* relationship = m_registry.try_get<RelationshipComponent>(handle);
            const EntityId parent_id = relationship
                ? relationship->parent
                : INVALID_ENTITY_ID;
            if(parent_id != INVALID_ENTITY_ID && parent_id != id
               && handles.contains(parent_id)) {
                children[parent_id].push_back(handle);
            } else {
                roots.push_back(handle);
            }
        }

        std::unordered_set<EntityId> visited;
        visited.reserve(handles.size());

        const auto update_subtree = [this, &children, &visited](
            const entt::entity handle,
            const Math::Mat4& parent_world,
            const auto& update_subtree_ref) -> void {
            const EntityId id = m_registry.get<IdComponent>(handle).id;
            if(!visited.insert(id).second) {
                return;
            }

            const auto* transform = m_registry.try_get<TransformComponent>(handle);
            auto& world_transform =
                m_registry.get_or_emplace<WorldTransformComponent>(handle);

            const Math::Mat4 local_matrix = transform
                ? transform->to_matrix()
                : Math::Mat4(1.0f);
            const Math::Mat4 camera_local_matrix = transform
                ? Math::compose_trs(
                    transform->translation,
                    transform->rotation,
                    Math::Vec3(1.0f))
                : Math::Mat4(1.0f);
            world_transform.world_matrix = parent_world * local_matrix;
            world_transform.camera_world_matrix =
                parent_world * camera_local_matrix;

            if(const auto child_handles = children.find(id);
               child_handles != children.end()) {
                for(const entt::entity child_handle: child_handles->second) {
                    update_subtree_ref(
                        child_handle,
                        world_transform.world_matrix,
                        update_subtree_ref);
                }
            }
        };

        for(const entt::entity root: roots) {
            update_subtree(root, Math::Mat4(1.0f), update_subtree);
        }
        for(const auto& [id, handle]: handles) {
            if(!visited.contains(id)) {
                update_subtree(handle, Math::Mat4(1.0f), update_subtree);
            }
        }
    }

    const Math::Mat4& Scene::get_world_matrix(const Entity entity) {
        if(!is_valid(entity)) {
            LOG_FATAL("Cannot get world matrix for an invalid entity");
        }

        update_world_transforms();
        return m_registry.get<WorldTransformComponent>(entity.m_handle).world_matrix;
    }

    Entity Scene::find_entity(const EntityId id) {
        if(id == INVALID_ENTITY_ID) {
            return {};
        }

        const auto view = m_registry.view<IdComponent>();
        for(const entt::entity handle: view) {
            if(view.get<IdComponent>(handle).id == id) {
                return {handle, this};
            }
        }

        return {};
    }

    std::vector<Entity> Scene::get_entities() {
        std::vector<Entity> entities;
        entities.reserve(entity_count());

        const auto view = m_registry.view<IdComponent>();
        for(const entt::entity handle: view) {
            entities.push_back(Entity(handle, this));
        }

        return entities;
    }

    bool Scene::is_valid(const Entity entity) const {
        return entity.m_scene == this
               && entity.m_handle != entt::null
               && m_registry.valid(entity.m_handle);
    }

    std::size_t Scene::entity_count() const {
        return m_registry.view<IdComponent>().size();
    }
}
