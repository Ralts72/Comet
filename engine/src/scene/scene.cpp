#include "scene/scene.h"
#include "scene/material_parameters.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include "common/scope_exit.h"
#include <unordered_set>

namespace Comet {
    namespace {
        std::atomic<uint64_t> next_material_instance{0};
    }

    Scene::Scene() {
        m_registry.on_destroy<MeshRendererComponent>().connect<&Scene::clear_material_overrides>(
            *this);
    }

    bool Scene::set_post_process(const PostProcessSettings& settings) {
        if(!settings.validate())
            return false;
        m_post_process = settings;
        return true;
    }

    bool Scene::set_environment(const SceneEnvironment& environment) {
        if(!environment.validate())
            return false;
        m_environment = environment;
        m_environment.rotation = Math::wrap_degrees(environment.rotation);
        return true;
    }

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
            m_dirty_transforms.erase(handle);
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
        mark_transform_dirty(handle);
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
            m_dirty_transforms.erase(it->m_handle);
            m_registry.destroy(it->m_handle);
        }
    }

    bool Scene::begin_runtime() {
        if(m_runtime_active)
            return false;
        m_entity_requests.clear();
        m_audio_play_requests.clear();
        m_contact_events.clear();
        m_session_values.clear();
        m_material_overrides.clear();
        m_restart_requested = false;
        m_runtime_active = true;
        return true;
    }

    std::optional<EntityUuid> Scene::request_create_entity(
        const std::string_view name, const EntityCreation& creation) {
        if(!m_runtime_active || m_entity_requests.size() >= MAX_ENTITY_REQUESTS || name.size() > 128
            || name.find('\0') != std::string_view::npos
            || !Math::is_finite(creation.transform.translation)
            || !Math::is_finite(creation.transform.rotation)
            || !Math::is_finite(creation.transform.scale))
            return std::nullopt;
        if(creation.mesh_renderer
            && (!creation.mesh_renderer->mesh || !creation.mesh_renderer->material))
            return std::nullopt;
        EntityUuid uuid;
        bool reserved = false;
        do {
            uuid = EntityUuid::generate();
            reserved = std::ranges::any_of(m_entity_requests, [uuid](const EntityRequest& request) {
                return request.type == EntityRequest::Type::Create && request.uuid == uuid;
            });
        } while(find_entity(uuid) || reserved);
        m_entity_requests.push_back({.type = EntityRequest::Type::Create,
            .uuid = uuid,
            .name = std::string(name),
            .creation = creation});
        return uuid;
    }

    bool Scene::request_destroy_entity(const Entity entity) {
        if(!m_runtime_active || !is_valid(entity))
            return false;
        const auto uuid = entity.get_uuid();
        const auto id = entity.get_id();
        if(std::ranges::any_of(m_entity_requests, [uuid, id](const EntityRequest& request) {
               return request.type == EntityRequest::Type::Destroy && request.uuid == uuid
                      && request.id == id;
           }))
            return true;
        if(m_entity_requests.size() >= MAX_ENTITY_REQUESTS)
            return false;
        m_entity_requests.push_back({.type = EntityRequest::Type::Destroy, .uuid = uuid, .id = id});
        return true;
    }

    bool Scene::request_play_one_shot(const Entity entity) {
        if(!m_runtime_active || !is_valid(entity) || !entity.has_component<AudioSourceComponent>()
            || m_audio_play_requests.size() >= MAX_AUDIO_PLAY_REQUESTS)
            return false;
        const auto& source = entity.get_component<AudioSourceComponent>();
        if(!source.clip || !std::isfinite(source.volume) || source.volume < 0 || source.volume > 1)
            return false;
        m_audio_play_requests.push_back({source.clip, source.volume});
        return true;
    }

    std::vector<Scene::AudioPlayRequest> Scene::take_audio_play_requests() {
        auto requests = std::move(m_audio_play_requests);
        m_audio_play_requests.clear();
        return requests;
    }

    bool Scene::request_restart() {
        if(!m_runtime_active)
            return false;
        m_restart_requested = true;
        return true;
    }

    bool Scene::take_restart_request() {
        return std::exchange(m_restart_requested, false);
    }

    bool Scene::commit_entity_requests() {
        auto requests = std::move(m_entity_requests);
        m_entity_requests.clear();
        for(const auto& request : requests) {
            if(request.type == EntityRequest::Type::Create) {
                auto entity = create_entity_with_uuid(request.uuid, request.name);
                if(!entity)
                    return false;
                // 新实体没有子节点；回滚直接移除索引，避免销毁遍历再次分配。
                const auto id = entity.get_id();
                ScopeExit rollback([&] {
                    m_entities_by_id.erase(id);
                    m_entities_by_uuid.erase(request.uuid);
                    m_dirty_transforms.erase(entity.m_handle);
                    m_registry.destroy(entity.m_handle);
                });
                if(!entity.try_set_transform(request.creation.transform))
                    return false;
                if(request.creation.mesh_renderer)
                    entity.add_component<MeshRendererComponent>(*request.creation.mesh_renderer);
                rollback.release();
                continue;
            }
            const Entity entity = find_entity(request.uuid);
            if(entity && entity.get_id() == request.id)
                destroy_entity(entity);
        }
        return true;
    }

    void Scene::end_runtime() noexcept {
        m_runtime_active = false;
        m_restart_requested = false;
        m_entity_requests.clear();
        m_audio_play_requests.clear();
        m_contact_events.clear();
        m_session_values.clear();
        m_material_overrides.clear();
    }

    std::optional<ParameterValue> Scene::get_session_value(const std::string_view key) const {
        if(!m_runtime_active || !valid_parameter_name(key))
            return std::nullopt;
        const auto found = m_session_values.find(std::string(key));
        if(found == m_session_values.end())
            return std::nullopt;
        return found->second;
    }

    bool Scene::set_session_value(const std::string_view key, ParameterValue value) {
        if(!m_runtime_active || !valid_parameter_name(key) || !valid_parameter_value(value)
            || std::holds_alternative<EntityUuid>(value)
            || std::holds_alternative<Math::Vec4>(value))
            return false;
        std::string name(key);
        if(m_session_values.size() >= 128 && !m_session_values.contains(name))
            return false;
        m_session_values.insert_or_assign(std::move(name), std::move(value));
        return true;
    }

    bool Scene::erase_session_value(const std::string_view key) {
        if(!m_runtime_active || !valid_parameter_name(key))
            return false;
        m_session_values.erase(std::string(key));
        return true;
    }

    void Scene::clear_material_overrides(entt::registry&, const entt::entity entity) {
        m_material_overrides.erase(entity);
    }

    std::shared_ptr<const MaterialOverrides> Scene::get_material_overrides(const Entity entity) {
        if(!m_runtime_active || !is_valid(entity))
            return nullptr;
        const auto found = m_material_overrides.find(entity.m_handle);
        if(found == m_material_overrides.end())
            return nullptr;
        if(!entity.has_component<MeshRendererComponent>()
            || entity.get_component<MeshRendererComponent>().material != found->second->material) {
            m_material_overrides.erase(found);
            return nullptr;
        }
        return found->second;
    }

    Result<void> Scene::set_material_scalar(const Entity entity, const std::string_view name,
        const float value, const MaterialParameterValidator& materials) {
        return set_material_parameter(entity, name, value, materials);
    }

    Result<void> Scene::set_material_vector(const Entity entity, const std::string_view name,
        const Math::Vec4 value, const MaterialParameterValidator& materials) {
        return set_material_parameter(entity, name, value, materials);
    }

    Result<void> Scene::set_material_parameter(const Entity entity, const std::string_view name,
        const std::variant<float, Math::Vec4> value, const MaterialParameterValidator& materials) {
        if(!m_runtime_active)
            return Result<void>::failure("Material overrides require an active scene runtime");
        if(!is_valid(entity) || !entity.has_component<MeshRendererComponent>())
            return Result<void>::failure("Current entity needs an existing Mesh Renderer");
        const auto previous = get_material_overrides(entity);
        const auto material = entity.get_component<MeshRendererComponent>().material;
        if(!material)
            return Result<void>::failure("Current Mesh Renderer needs a material");
        if(name.empty() || name.find('\0') != std::string_view::npos)
            return Result<void>::failure("Expected a material property name");
        MaterialOverrides candidate = previous ? *previous : MaterialOverrides{};
        candidate.material = material;
        if(!previous)
            candidate.instance_id =
                next_material_instance.fetch_add(1, std::memory_order_relaxed) + 1;
        const std::string property(name);
        if(const auto* scalar = std::get_if<float>(&value)) {
            if(!std::isfinite(*scalar))
                return Result<void>::failure("Material scalar must be finite");
            candidate.vector_properties.erase(property);
            candidate.scalar_properties.insert_or_assign(property, *scalar);
        } else {
            const auto vector = std::get<Math::Vec4>(value);
            if(!Math::is_finite(vector))
                return Result<void>::failure("Material vector must be finite");
            candidate.scalar_properties.erase(property);
            candidate.vector_properties.insert_or_assign(property, vector);
        }
        // 相同值也重验，防止已发布 Shader 布局变更后接受陈旧参数。
        if(auto checked = materials.validate(candidate); !checked)
            return checked;
        if(previous && previous->scalar_properties == candidate.scalar_properties
            && previous->vector_properties == candidate.vector_properties)
            return Result<void>::success();
        m_material_overrides.insert_or_assign(
            entity.m_handle, std::make_shared<const MaterialOverrides>(std::move(candidate)));
        return Result<void>::success();
    }

    bool Scene::append_contact_event(ContactEvent event) {
        if(m_contact_events.size() >= 8192)
            return false;
        m_contact_events.push_back(event);
        return true;
    }

    void Scene::clear_contact_events() noexcept {
        m_contact_events.clear();
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
        mark_transform_dirty(child.m_handle);
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
        mark_transform_dirty(child.m_handle);
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

    void Scene::mark_transform_dirty(const entt::entity handle) {
        m_transform_work.clear();
        m_transform_work.push_back(handle);
        for(std::size_t i = 0; i < m_transform_work.size(); ++i) {
            const auto current = m_transform_work[i];
            if(!m_dirty_transforms.insert(current).second)
                continue;
            const auto children =
                m_children_by_parent.find(m_registry.get<IdComponent>(current).id);
            if(children != m_children_by_parent.end())
                m_transform_work.insert(
                    m_transform_work.end(), children->second.begin(), children->second.end());
        }
    }

    void Scene::update_world_transform(const entt::entity handle) {
        const EntityId parent = m_registry.get<RelationshipComponent>(handle).parent;
        const auto* local = m_registry.try_get<TransformComponent>(handle);

        const WorldTransformComponent* parent_world = nullptr;
        if(parent != INVALID_ENTITY_ID)
            parent_world = &m_registry.get<WorldTransformComponent>(m_entities_by_id.at(parent));
        auto& world = m_registry.get<WorldTransformComponent>(handle);
        Math::Mat4 pose_local(1);
        if(local)
            pose_local = Math::compose_trs(local->translation, local->rotation, Math::Vec3(1));
        const Math::Mat4 local_matrix = local ? Math::scale(pose_local, local->scale) : pose_local;
        world.world_matrix = local_matrix;
        world.pose_world_matrix = pose_local;
        if(parent_world) {
            world.world_matrix = parent_world->world_matrix * local_matrix;
            world.pose_world_matrix = parent_world->pose_world_matrix * pose_local;
        }
        world.pose_world_matrix[3] = world.world_matrix[3];
        m_dirty_transforms.erase(handle);
    }

    std::size_t Scene::sync_transform_chain(entt::entity handle) {
        m_transform_work.clear();
        while(m_dirty_transforms.contains(handle)) {
            m_transform_work.push_back(handle);
            const auto parent = m_registry.get<RelationshipComponent>(handle).parent;
            if(parent == INVALID_ENTITY_ID)
                break;
            handle = m_entities_by_id.at(parent);
        }
        for(auto it = m_transform_work.rbegin(); it != m_transform_work.rend(); ++it)
            update_world_transform(*it);
        return m_transform_work.size();
    }

    std::size_t Scene::update_world_transforms() {
        std::size_t updated = 0;
        while(!m_dirty_transforms.empty())
            updated += sync_transform_chain(*m_dirty_transforms.begin());
        return updated;
    }

    const Math::Mat4& Scene::get_world_matrix(const Entity entity) {
        if(!is_valid(entity)) {
            LOG_FATAL("Cannot get world matrix for an invalid entity");
        }

        sync_transform_chain(entity.m_handle);
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
