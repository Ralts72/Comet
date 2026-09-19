#include "scene/scene.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include "common/scope_exit.h"
#include <unordered_set>

namespace Comet {
    Entity Scene::create_entity(const std::string& name) {
        EntityUuid uuid;
        do {
            uuid = EntityUuid::generate();
        } while(find_entity(uuid));
        return create_entity_with_uuid(uuid, name);
    }

    Entity Scene::create_entity_with_uuid(const EntityUuid uuid, const std::string& name) {
        if(!uuid || find_entity(uuid)) {
            return {};
        }

        const entt::entity handle = m_registry.create();
        Entity entity(handle, this);
        const EntityId id = m_next_entity_id;
        bool id_indexed = false;
        bool uuid_indexed = false;

        ScopeExit rollback([&] {
            if(id_indexed)
                m_entities_by_id.erase(id);
            if(uuid_indexed)
                m_entities_by_uuid.erase(uuid);
            m_registry.destroy(handle);
        });
        m_registry.emplace<IdComponent>(handle, id);
        m_registry.emplace<UuidComponent>(handle, uuid);
        m_registry.emplace<NameComponent>(handle, name.empty() ? "Entity" : name);
        m_registry.emplace<TransformComponent>(handle);
        m_registry.emplace<RelationshipComponent>(handle);
        m_registry.emplace<WorldTransformComponent>(handle);
        id_indexed = m_entities_by_id.emplace(id, handle).second;
        uuid_indexed = m_entities_by_uuid.emplace(uuid, handle).second;
        if(!id_indexed || !uuid_indexed) {
            LOG_FATAL("Scene entity index is inconsistent");
        }
        rollback.release();
        ++m_next_entity_id;
        return entity;
    }

    void Scene::destroy_entity(const Entity entity) {
        if(!is_valid(entity)) {
            return;
        }

        std::unordered_set<EntityId> destroying;
        std::vector<Entity> pending{entity};
        std::vector<Entity> subtree;
        for(std::size_t i = 0; i < pending.size(); ++i) {
            const auto current = pending[i];
            if(!is_valid(current) || !destroying.insert(current.get_id()).second)
                continue;
            subtree.push_back(current);
            const auto children = get_children(current);
            pending.insert(pending.end(), children.begin(), children.end());
        }
        // 所有遍历分配在删除前完成，避免半途分配失败留下半棵树。
        remove_child_index(entity.get_component<RelationshipComponent>().parent, entity.m_handle);
        for(auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
            const EntityId id = it->get_id();
            const EntityUuid uuid = it->get_uuid();
            m_children_by_parent.erase(id);
            m_entities_by_id.erase(id);
            m_entities_by_uuid.erase(uuid);
            m_transform_states.erase(id);
            m_registry.destroy(it->m_handle);
        }
    }

    bool Scene::set_parent(const Entity child, const Entity parent) {
        if(!is_valid(child) || !is_valid(parent) || child == parent || has_cycle(child, parent)) {
            return false;
        }

        auto& relationship = m_registry.get_or_emplace<RelationshipComponent>(child.m_handle);
        if(relationship.parent == parent.get_id()) {
            return true;
        }

        const EntityId previous_parent = relationship.parent;
        const EntityId new_parent = parent.get_id();
        // 先完成可能失败的分配，再修改已有关系；新列表构造成功后才加入索引。
        const auto children = m_children_by_parent.find(new_parent);
        if(children == m_children_by_parent.end())
            m_children_by_parent.emplace(new_parent, std::vector{child.m_handle});
        else
            children->second.push_back(child.m_handle);
        relationship.parent = new_parent;
        remove_child_index(previous_parent, child.m_handle);
        return true;
    }

    bool Scene::clear_parent(const Entity child) {
        if(!is_valid(child)) {
            return false;
        }

        auto& relationship = m_registry.get_or_emplace<RelationshipComponent>(child.m_handle);
        if(relationship.parent == INVALID_ENTITY_ID) {
            return true;
        }

        const EntityId previous_parent = relationship.parent;
        relationship.parent = INVALID_ENTITY_ID;
        remove_child_index(previous_parent, child.m_handle);
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

        const auto indexed = m_children_by_parent.find(entity.get_id());
        if(indexed == m_children_by_parent.end()) {
            return children;
        }
        children.reserve(indexed->second.size());
        for(const entt::entity handle : indexed->second)
            children.push_back(Entity(handle, this));
        std::ranges::sort(children, {}, &Entity::get_id);
        return children;
    }

    std::vector<Entity> Scene::get_root_entities() {
        std::vector<Entity> roots;
        roots.reserve(m_entities_by_id.size());
        for(const auto& [id, handle] : m_entities_by_id) {
            static_cast<void>(id);
            if(m_registry.get<RelationshipComponent>(handle).parent == INVALID_ENTITY_ID)
                roots.push_back(Entity(handle, this));
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

    bool Scene::update_world_transform(const entt::entity handle) {
        const EntityId id = m_registry.get<IdComponent>(handle).id;
        const EntityId parent = m_registry.get<RelationshipComponent>(handle).parent;
        const auto* local = m_registry.try_get<TransformComponent>(handle);
        const uint64_t parent_version =
            parent == INVALID_ENTITY_ID ? 0 : m_transform_states.at(parent).version;
        auto& state = m_transform_states[id];
        const bool same_local = state.has_local == (local != nullptr)
                                && (!local
                                    || (state.local.translation == local->translation
                                        && state.local.rotation == local->rotation
                                        && state.local.scale == local->scale));
        if(state.version != 0 && state.parent == parent && state.parent_version == parent_version
            && same_local)
            return false;

        const auto* parent_world =
            parent == INVALID_ENTITY_ID
                ? nullptr
                : &m_registry.get<WorldTransformComponent>(m_entities_by_id.at(parent));
        auto& world = m_registry.get<WorldTransformComponent>(handle);
        const Math::Mat4 pose_local =
            local ? Math::compose_trs(local->translation, local->rotation, Math::Vec3(1))
                  : Math::Mat4(1);
        const Math::Mat4 local_matrix = local ? Math::scale(pose_local, local->scale) : pose_local;
        world.world_matrix =
            parent_world ? parent_world->world_matrix * local_matrix : local_matrix;
        world.pose_world_matrix =
            parent_world ? parent_world->pose_world_matrix * pose_local : pose_local;
        world.pose_world_matrix[3] = world.world_matrix[3];
        state.local = local ? *local : TransformComponent{};
        state.has_local = local != nullptr;
        state.parent = parent;
        state.parent_version = parent_version;
        ++state.version;
        return true;
    }

    std::size_t Scene::update_world_transforms() {
        std::vector<entt::entity> pending;
        pending.reserve(m_entities_by_id.size());
        for(const auto& [id, handle] : m_entities_by_id) {
            if(m_registry.get<RelationshipComponent>(handle).parent == INVALID_ENTITY_ID)
                pending.push_back(handle);
        }
        std::size_t updated = 0;
        for(std::size_t index = 0; index < pending.size(); ++index) {
            const auto handle = pending[index];
            updated += update_world_transform(handle);
            const auto children = m_children_by_parent.find(m_registry.get<IdComponent>(handle).id);
            if(children != m_children_by_parent.end()) {
                for(const auto child : children->second)
                    pending.push_back(child);
            }
        }
        return updated;
    }

    const Math::Mat4& Scene::get_world_matrix(const Entity entity) {
        if(!is_valid(entity)) {
            LOG_FATAL("Cannot get world matrix for an invalid entity");
        }

        std::vector<entt::entity> ancestors;
        for(Entity current = entity; current; current = get_parent(current))
            ancestors.push_back(current.m_handle);
        for(auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            update_world_transform(*it);
        return m_registry.get<WorldTransformComponent>(entity.m_handle).world_matrix;
    }

    Entity Scene::find_entity(const EntityId id) {
        if(id == INVALID_ENTITY_ID) {
            return {};
        }

        const auto entity = m_entities_by_id.find(id);
        return entity == m_entities_by_id.end() ? Entity{} : Entity(entity->second, this);
    }

    Entity Scene::find_entity(const EntityUuid uuid) {
        if(!uuid) {
            return {};
        }

        const auto entity = m_entities_by_uuid.find(uuid);
        return entity == m_entities_by_uuid.end() ? Entity{} : Entity(entity->second, this);
    }

    std::vector<Entity> Scene::get_entities() {
        std::vector<Entity> entities;
        entities.reserve(m_entities_by_id.size());
        for(const auto& [id, handle] : m_entities_by_id) {
            static_cast<void>(id);
            entities.push_back(Entity(handle, this));
        }
        std::ranges::sort(entities, {}, &Entity::get_id);

        return entities;
    }

    bool Scene::is_valid(const Entity entity) const {
        return entity.m_scene == this && entity.m_handle != entt::null
               && m_registry.valid(entity.m_handle);
    }

    std::size_t Scene::entity_count() const {
        return m_entities_by_id.size();
    }

    void Scene::remove_child_index(const EntityId parent, const entt::entity child) {
        if(parent == INVALID_ENTITY_ID)
            return;
        const auto children = m_children_by_parent.find(parent);
        if(children == m_children_by_parent.end())
            return;
        std::erase(children->second, child);
        if(children->second.empty())
            m_children_by_parent.erase(children);
    }
}
