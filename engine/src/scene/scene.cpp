#include "scene/scene.h"
#include "scene/prefab.h"
#include "scene/material_parameters.h"
#include "asset/data/model_data.h"
#include "animation/animation_data.h"

#include "diagnostics/logger.h"

#include <algorithm>
#include "common/version.h"
#include <cmath>
#include "common/scope_exit.h"
#include "common/binary.h"
#include <unordered_set>

namespace Comet {
    namespace {
        VersionSequence next_material_instance;
        VersionSequence next_scene_lifetime;

        EntityUuid model_source_uuid(AssetHandle source, std::string_view key) {
            const auto hash = Binary::hash_bytes(std::as_bytes(std::span(key.data(), key.size())));
            EntityUuid::Bytes bytes{};
            for(std::size_t i = 0; i < 8; ++i) {
                bytes[i] = static_cast<uint8_t>(source.value() >> (i * 8));
                bytes[i + 8] = static_cast<uint8_t>(hash >> (i * 8));
            }
            return EntityUuid(bytes);
        }
    }

    Scene::Scene() : m_lifetime(next_scene_lifetime.next()) {
        m_registry.on_destroy<MeshRendererComponent>().connect<&Scene::clear_material_overrides>(
            *this);
    }

    bool Scene::set_post_process(const PostProcessSettings& settings) {
        if(!settings.validate())
            return false;
        if(m_post_process != settings) {
            m_post_process = settings;
            mark_render_dirty();
        }
        return true;
    }

    bool Scene::set_environment(const SceneEnvironment& environment) {
        if(!environment.validate())
            return false;
        auto candidate = environment;
        candidate.rotation = Math::wrap_degrees(candidate.rotation);
        if(m_environment != candidate) {
            m_environment = candidate;
            mark_render_dirty();
        }
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
            m_dirty_transforms.remove(handle);
            m_dirty_render_entities.remove(handle);
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
        m_transform_interpolation.discontinuity(*this, handle);
        m_render_structure_changed = true;
        rollback.release();
        ++m_next_entity_id;
        return entity;
    }

    Result<Entity> Scene::instantiate_model(const ModelData& model, const std::string& name,
        Math::Vec3 position, std::optional<uint32_t> model_scene) {
        return instantiate_model_content(model, name, position, model_scene, {});
    }

    Result<Entity> Scene::instantiate_model_content(const ModelData& model, const std::string& name,
        Math::Vec3 position, std::optional<uint32_t> model_scene, AssetHandle identity_source) {
        const auto selected = model_scene.value_or(model.default_scene);
        if(selected >= model.scenes.size() || !Math::is_finite(position))
            return Result<Entity>::failure("Invalid model scene or placement");
        std::vector<std::vector<uint32_t>> children(model.nodes.size());
        std::unordered_set<std::string_view> keys;
        for(uint32_t index = 0; index < model.nodes.size(); ++index) {
            const auto& node = model.nodes[index];
            if(identity_source && (node.key.empty() || !keys.insert(node.key).second))
                return Result<Entity>::failure("Model source keys must be nonempty and unique");
            if(node.parent != ModelData::NO_PARENT) {
                if(node.parent >= model.nodes.size())
                    return Result<Entity>::failure("Invalid model parent");
                children[node.parent].push_back(index);
            }
            const auto& t = node.transform;
            if(!Math::is_finite(t.translation) || !Math::is_finite(t.scale)
                || !Math::is_finite(
                    Math::Vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w))
                || std::abs(glm::length(t.rotation) - 1.f) > 1e-3f)
                return Result<Entity>::failure("Invalid model transform");
            for(const auto& part : node.parts)
                if(!part.mesh || !part.material)
                    return Result<Entity>::failure("Invalid model part references");
        }
        auto order = model.scenes[selected].roots;
        std::vector<bool> visited(model.nodes.size());
        for(const auto root : order)
            if(root >= model.nodes.size() || model.nodes[root].parent != ModelData::NO_PARENT)
                return Result<Entity>::failure("Invalid model scene root");
        for(size_t i = 0; i < order.size(); ++i) {
            const auto node = order[i];
            if(visited[node])
                return Result<Entity>::failure("Model contains repeated or cyclic nodes");
            visited[node] = true;
            order.insert(order.end(), children[node].begin(), children[node].end());
        }
        const auto create = [&](const std::string& label, const std::string& key) {
            return identity_source
                       ? create_entity_with_uuid(model_source_uuid(identity_source, key), label)
                       : create_entity(label);
        };
        auto root = create(name, "root");
        if(!root)
            return Result<Entity>::failure("Model root identity collision");
        ScopeExit rollback([&] {
            if(root)
                destroy_entity(root);
        });
        root.edit_transform([&](TransformComponent& value) { value.translation = position; });
        std::vector<Entity> entities(model.nodes.size());
        for(const auto index : order) {
            const auto& node = model.nodes[index];
            // 长度前缀区分节点键和 primitive 序号，身份不依赖父节点或遍历顺序。
            const auto key = "node/" + std::to_string(node.key.size()) + ":" + node.key;
            auto entity = create(node.name.empty() ? "Node" : node.name, key);
            if(!entity)
                return Result<Entity>::failure("Model node identity collision");
            // 先挂入根，后续失败可以完整撤销整棵候选树。
            if(!set_parent(entity, root)) {
                destroy_entity(entity);
                return Result<Entity>::failure("Cannot attach model node");
            }
            entities[index] = entity;
            entity.edit_transform([&](TransformComponent& value) {
                value.translation = node.transform.translation;
                value.rotation = glm::degrees(glm::eulerAngles(node.transform.rotation));
                value.scale = node.transform.scale;
            });
            if(node.parent != ModelData::NO_PARENT && !set_parent(entity, entities[node.parent]))
                return Result<Entity>::failure("Cannot attach model hierarchy");
            for(size_t part = 0; part < node.parts.size(); ++part) {
                auto primitive = create("Primitive " + std::to_string(part + 1),
                    "part/" + key + "/" + std::to_string(part));
                if(!primitive)
                    return Result<Entity>::failure("Model primitive identity collision");
                if(!set_parent(primitive, entity)) {
                    destroy_entity(primitive);
                    return Result<Entity>::failure("Cannot attach model primitive");
                }
                primitive.add_component<MeshRendererComponent>(
                    MeshRendererComponent{node.parts[part].mesh, node.parts[part].material});
            }
        }
        rollback.release();
        return Result<Entity>::success(root);
    }

    void Scene::destroy_entity(const Entity entity) {
        if(!is_valid(entity)) {
            return;
        }

        std::vector<entt::entity> subtree{entity.m_handle};
        for(std::size_t i = 0; i < subtree.size(); ++i) {
            const auto id = m_registry.get<IdComponent>(subtree[i]).id;
            const auto children = m_children_by_parent.find(id);
            if(children != m_children_by_parent.end())
                for(const auto child : children->second)
                    subtree.push_back(child);
        }
        // 所有遍历分配在删除前完成，避免半途分配失败留下半棵树。
        remove_child_index(entity.get_component<RelationshipComponent>().parent, entity.m_handle);
        for(auto it = subtree.rbegin(); it != subtree.rend(); ++it) {
            const EntityId id = m_registry.get<IdComponent>(*it).id;
            const EntityUuid uuid = m_registry.get<UuidComponent>(*it).uuid;
            mark_render_entity_changed(*it);
            m_render_structure_changed = true;
            m_children_by_parent.erase(id);
            m_entities_by_id.erase(id);
            m_entities_by_uuid.erase(uuid);
            m_dirty_transforms.remove(*it);
            m_dirty_render_entities.remove(*it);
            m_dirty_physics_entities.remove(*it);
            m_transform_interpolation.forget(*it);
            if(m_registry.any_of<RigidBodyComponent, CharacterControllerComponent>(*it))
                m_physics_structure_changed = true;
            if(m_registry.any_of<PointJointComponent, HingeJointComponent, SliderJointComponent>(
                   *it))
                m_physics_joints_changed = true;
            m_animation_playbacks.erase(*it);
            m_animation_sources.erase(*it);
            m_registry.destroy(*it);
        }
    }

    std::shared_ptr<const AnimationPlayback> Scene::get_animation_playback(Entity entity) const {
        if(!is_valid(entity))
            return nullptr;
        const auto found = m_animation_playbacks.find(entity.m_handle);
        return found == m_animation_playbacks.end() ? nullptr : found->second;
    }
    void Scene::set_animation_playback(
        Entity entity, std::shared_ptr<const AnimationPlayback> playback) {
        if(!is_valid(entity))
            return;
        const auto found = m_animation_playbacks.find(entity.m_handle);
        if(found != m_animation_playbacks.end() && playback && *found->second == *playback)
            return;
        if(!playback && !m_animation_playbacks.erase(entity.m_handle))
            return;
        if(playback) {
            if(const auto binding = m_animation_sources.find(entity.m_handle);
                binding != m_animation_sources.end()) {
                auto frame = std::make_shared<AnimationPlayback>(*playback);
                frame->pose_source = binding->second.data;
                auto pose =
                    resolve_animation_pose(*frame->pose_source, frame.get(), frame->pose_error);
                frame->pose =
                    pose ? std::make_shared<const AnimationPose>(std::move(pose).value()) : nullptr;
                playback = std::move(frame);
            }
            m_animation_playbacks.insert_or_assign(entity.m_handle, std::move(playback));
        }
        mark_render_entity_changed(entity.m_handle);
        mark_attachments_dirty(entity.m_handle);
    }
    void Scene::set_animation_source(Entity entity, std::shared_ptr<const AnimationData> source) {
        if(!is_valid(entity))
            return;
        const auto children = m_children_by_parent.find(entity.get_id());
        if(children == m_children_by_parent.end()
            || std::ranges::none_of(children->second,
                [&](auto child) { return m_registry.any_of<BoneAttachmentComponent>(child); }))
            source.reset();
        const auto found = m_animation_sources.find(entity.m_handle);
        if(found == m_animation_sources.end() && !source)
            return;
        if(found != m_animation_sources.end() && found->second.data == source)
            return;
        if(source) {
            auto pose = sample_animation(*source, nullptr);
            m_animation_sources.insert_or_assign(entity.m_handle,
                AnimationSource{
                    source, pose ? std::make_shared<const AnimationPose>(std::move(pose).value())
                                 : nullptr});
        } else {
            m_animation_sources.erase(entity.m_handle);
        }
        // 模型重导入使当前采样失效，保持时钟并重新求值。
        const auto current = get_animation_playback(entity);
        if(current) {
            auto input = std::make_shared<AnimationPlayback>(*current);
            input->pose.reset();
            input->pose_source.reset();
            input->pose_error.clear();
            m_animation_playbacks.erase(entity.m_handle);
            set_animation_playback(entity, std::move(input));
        }
        mark_attachments_dirty(entity.m_handle);
    }
    void Scene::mark_attachments_dirty(entt::entity parent) {
        const auto children = m_children_by_parent.find(m_registry.get<IdComponent>(parent).id);
        if(children == m_children_by_parent.end())
            return;
        for(const auto child : children->second)
            if(m_registry.any_of<BoneAttachmentComponent>(child))
                mark_transform_dirty(child);
    }

    bool Scene::request_particle_burst(Entity entity, const uint32_t count) {
        if(!m_runtime_active || !is_valid(entity)
            || !entity.has_component<ParticleEmitterComponent>() || count == 0
            || count > ParticleEmitterComponent::MAX_CAPACITY || m_particle_requests.size() >= 128)
            return false;
        const auto& settings = entity.get_component<ParticleEmitterComponent>();
        if(!settings.enabled || !settings.validate())
            return false;
        (void)get_world_matrix(entity);
        const auto& world = entity.get_component<WorldTransformComponent>();
        auto pose = world.pose_world_matrix;
        pose[3] = world.world_matrix[3];
        m_particle_requests.push_back({entity.get_id(), settings, pose, count});
        return true;
    }

    bool Scene::request_particle_clear(Entity entity) {
        if(!m_runtime_active || !is_valid(entity) || m_particle_requests.size() >= 128)
            return false;
        m_particle_requests.push_back({entity.get_id(), {}, Math::Mat4(1), 0, true});
        return true;
    }

    void Scene::set_particles(std::shared_ptr<const ParticleSnapshot> particles) {
        if(m_particles == particles)
            return;
        m_particles = std::move(particles);
        mark_render_dirty();
    }

    bool Scene::begin_runtime() {
        if(m_runtime_active)
            return false;
        clear_runtime_state();
        m_runtime_active = true;
        return true;
    }

    std::optional<EntityUuid> Scene::request_create_entity(
        const std::string_view name, const EntityCreation& creation) {
        if(!can_queue_entity_creation(1) || name.size() > 128
            || name.find('\0') != std::string_view::npos
            || !Math::is_finite(creation.transform.translation)
            || !Math::is_finite(creation.transform.rotation)
            || !Math::is_finite(creation.transform.scale))
            return std::nullopt;
        if(creation.mesh_renderer
            && (!creation.mesh_renderer->mesh || !creation.mesh_renderer->material))
            return std::nullopt;
        const auto uuid = reserve_entity_uuid();
        m_entity_requests.push_back({.type = EntityRequest::Type::Create,
            .uuid = uuid,
            .name = std::string(name),
            .creation = creation});
        return uuid;
    }

    bool Scene::can_queue_entity_creation(std::size_t count) const {
        if(!m_runtime_active || count > MAX_ENTITY_REQUESTS
            || m_entity_requests.size() >= MAX_ENTITY_REQUESTS)
            return false;
        auto remaining = MAX_ENTITY_REQUESTS - count;
        for(const auto& request : m_entity_requests) {
            const auto queued = request.prefab ? request.prefab->entity_count()
                                               : static_cast<std::size_t>(
                                                     request.type == EntityRequest::Type::Create);
            if(queued > remaining)
                return false;
            remaining -= queued;
        }
        return true;
    }

    EntityUuid Scene::reserve_entity_uuid() const {
        EntityUuid uuid;
        bool reserved;
        do {
            uuid = EntityUuid::generate();
            reserved = std::ranges::any_of(m_entity_requests,
                [uuid](const EntityRequest& request) { return request.uuid == uuid; });
        } while(m_entities_by_uuid.contains(uuid) || reserved);
        return uuid;
    }

    std::optional<EntityUuid> Scene::request_instantiate_prefab(
        std::shared_ptr<const Prefab> prefab, std::shared_ptr<const ComponentRegistry> components,
        std::optional<Math::Vec3> position) {
        if(!prefab || !components || !can_queue_entity_creation(prefab->entity_count())
            || (position && !Math::is_finite(*position)))
            return std::nullopt;
        const auto uuid = reserve_entity_uuid();
        m_entity_requests.push_back({.type = EntityRequest::Type::InstantiatePrefab,
            .uuid = uuid,
            .prefab = std::move(prefab),
            .components = std::move(components),
            .position = position});
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

    bool Scene::request_remove_rigid_body(const Entity entity) {
        if(!m_runtime_active || !is_valid(entity))
            return false;
        if(!entity.has_component<RigidBodyComponent>())
            return true;
        const auto uuid = entity.get_uuid();
        const auto id = entity.get_id();
        if(std::ranges::any_of(m_entity_requests, [uuid, id](const EntityRequest& request) {
               return request.type == EntityRequest::Type::RemoveRigidBody && request.uuid == uuid
                      && request.id == id;
           }))
            return true;
        if(m_entity_requests.size() >= MAX_ENTITY_REQUESTS)
            return false;
        m_entity_requests.push_back(
            {.type = EntityRequest::Type::RemoveRigidBody, .uuid = uuid, .id = id});
        return true;
    }

    bool Scene::commit_entity_requests() {
        auto requests = std::move(m_entity_requests);
        m_entity_requests.clear();
        for(const auto& request : requests) {
            switch(request.type) {
                case EntityRequest::Type::Create: {
                    auto entity = create_entity_with_uuid(request.uuid, request.name);
                    if(!entity)
                        return false;
                    // 新实体没有子节点；回滚直接移除索引，避免销毁遍历再次分配。
                    const auto id = entity.get_id();
                    ScopeExit rollback([&] {
                        m_entities_by_id.erase(id);
                        m_entities_by_uuid.erase(request.uuid);
                        m_dirty_transforms.remove(entity.m_handle);
                        m_dirty_render_entities.remove(entity.m_handle);
                        m_registry.destroy(entity.m_handle);
                    });
                    if(!entity.try_set_transform(request.creation.transform))
                        return false;
                    if(request.creation.mesh_renderer)
                        entity.add_component<MeshRendererComponent>(
                            *request.creation.mesh_renderer);
                    rollback.release();
                    break;
                }
                case EntityRequest::Type::InstantiatePrefab: {
                    const auto instance = request.prefab->instantiate(*this, *request.components,
                        request.position, {}, {}, AssetType::Prefab, request.uuid);
                    if(!instance) {
                        LOG_ERROR("Cannot instantiate prefab: {}", instance.error());
                        return false;
                    }
                    break;
                }
                case EntityRequest::Type::Destroy: {
                    const Entity entity = find_entity(request.uuid);
                    if(entity && entity.get_id() == request.id)
                        destroy_entity(entity);
                    break;
                }
                case EntityRequest::Type::RemoveRigidBody: {
                    const Entity entity = find_entity(request.uuid);
                    if(entity && entity.get_id() == request.id)
                        entity.remove_component<RigidBodyComponent>();
                    break;
                }
            }
        }
        return true;
    }

    void Scene::end_runtime() noexcept {
        m_runtime_active = false;
        clear_runtime_state();
    }

    void Scene::clear_runtime_state() noexcept {
        m_transform_interpolation.reset(*this);
        m_entity_requests.clear();
        m_contact_events.clear();
        m_events.clear();
        m_particle_requests.clear();
        set_particles(nullptr);
        m_dirty_physics_entities.clear();
        m_physics_structure_changed = false;
        m_physics_joints_changed = false;
        if(!m_material_overrides.empty() || !m_animation_playbacks.empty()) {
            mark_render_dirty();
            m_render_full_update = true;
        }
        m_material_overrides.clear();
        for(const auto& [handle, playback] : m_animation_playbacks)
            mark_attachments_dirty(handle);
        m_animation_playbacks.clear();
    }

    bool Scene::emit_event(
        const std::string_view name, std::optional<ParameterValue> value, Entity target) {
        if(!m_runtime_active || m_events.size() >= MAX_EVENTS || !valid_parameter_name(name))
            return false;
        if(value
            && (!valid_parameter_value(*value) || std::holds_alternative<EntityUuid>(*value)
                || std::holds_alternative<Math::Vec4>(*value)))
            return false;
        if(target && !is_valid(target))
            return false;
        m_events.push_back(
            {std::string(name), std::move(value), target ? target.get_uuid() : EntityUuid{}});
        return true;
    }

    std::vector<Scene::Event> Scene::take_events() {
        return std::exchange(m_events, {});
    }

    void Scene::clear_material_overrides(entt::registry&, const entt::entity entity) {
        m_material_overrides.erase(entity);
    }

    std::shared_ptr<const MaterialOverrides> Scene::get_material_overrides(const Entity entity) {
        if(!m_runtime_active || m_material_overrides.empty() || !is_valid(entity))
            return nullptr;
        const auto found = m_material_overrides.find(entity.m_handle);
        if(found == m_material_overrides.end())
            return nullptr;
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
            candidate.instance_id = next_material_instance.next();
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
        mark_render_entity_changed(entity.m_handle);
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
        if(!is_valid(child) || !is_valid(parent) || child == parent) {
            return false;
        }

        auto& relationship = m_registry.get<RelationshipComponent>(child.m_handle);
        if(relationship.parent == parent.get_id()) {
            return true;
        }
        if(has_cycle(child, parent))
            return false;

        const EntityId previous_parent = relationship.parent;
        const EntityId new_parent = parent.get_id();
        // 先完成可能失败的分配，再修改已有关系；新列表构造成功后才加入索引。
        const auto children = m_children_by_parent.find(new_parent);
        if(children == m_children_by_parent.end())
            m_children_by_parent.emplace(new_parent, std::vector{child.m_handle});
        else
            children->second.push_back(child.m_handle);
        relationship.parent = new_parent;
        m_transform_interpolation.discontinuity(*this, child.m_handle);
        remove_child_index(previous_parent, child.m_handle);
        mark_transform_dirty(child.m_handle);
        mark_physics_dirty(child.m_handle);
        return true;
    }

    bool Scene::clear_parent(const Entity child) {
        if(!is_valid(child)) {
            return false;
        }

        auto& relationship = m_registry.get<RelationshipComponent>(child.m_handle);
        if(relationship.parent == INVALID_ENTITY_ID) {
            return true;
        }

        const EntityId previous_parent = relationship.parent;
        relationship.parent = INVALID_ENTITY_ID;
        m_transform_interpolation.discontinuity(*this, child.m_handle);
        remove_child_index(previous_parent, child.m_handle);
        mark_transform_dirty(child.m_handle);
        mark_physics_dirty(child.m_handle);
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
        // 关系只由 Scene 修改，已有层级始终无环。
        Entity ancestor = parent;
        while(ancestor) {
            if(ancestor == child) {
                return true;
            }
            ancestor = get_parent(ancestor);
        }
        return false;
    }

    Scene::RenderChanges Scene::render_changes() const {
        return {m_render_revision, m_render_changes_base, m_render_full_update,
            m_render_structure_changed, m_render_changes};
    }

    void Scene::acknowledge_render_changes(const uint64_t revision, const std::size_t next_limit) {
        if(revision != m_render_revision)
            return;
        m_render_changes_base = revision;
        m_render_change_limit = next_limit;
        m_render_full_update = false;
        m_render_structure_changed = false;
        m_render_changes.clear();
        m_dirty_render_entities.clear();
    }

    Scene::PhysicsChanges Scene::physics_changes() const {
        return {m_physics_structure_changed, m_physics_joints_changed,
            {m_dirty_physics_entities.data(), m_dirty_physics_entities.size()}};
    }

    void Scene::acknowledge_physics_changes() {
        m_dirty_physics_entities.clear();
        m_physics_structure_changed = false;
        m_physics_joints_changed = false;
    }

    void Scene::mark_render_entity_changed(const entt::entity handle) {
        mark_render_dirty();
        record_render_change(handle);
    }

    void Scene::record_render_change(const entt::entity handle) {
        if(m_render_full_update || m_dirty_render_entities.contains(handle))
            return;
        // 大批变化改走顺序提取，队列也不会随离线增删持续增长。
        if(m_render_changes.size() >= m_render_change_limit) {
            m_render_full_update = true;
            return;
        }
        m_render_changes.push_back({handle, m_registry.get<IdComponent>(handle).id});
        m_dirty_render_entities.push(handle);
    }

    void Scene::mark_transform_dirty(const entt::entity handle) {
        if(m_dirty_transforms.contains(handle))
            return;
        mark_render_entity_changed(handle);
        m_dirty_transforms.push(handle);
        if(m_children_by_parent.empty())
            return;
        m_transform_work.clear();
        m_transform_work.push_back(handle);
        for(std::size_t i = 0; i < m_transform_work.size(); ++i) {
            const auto children =
                m_children_by_parent.find(m_registry.get<IdComponent>(m_transform_work[i]).id);
            if(children == m_children_by_parent.end())
                continue;
            for(const auto child : children->second) {
                // 已脏节点的子树也已标记，传播队列只保存首次变脏的节点。
                if(m_dirty_transforms.contains(child))
                    continue;
                m_dirty_transforms.push(child);
                record_render_change(child);
                m_transform_work.push_back(child);
            }
        }
    }

    void Scene::reset_transform_interpolation(const entt::entity handle) {
        m_transform_interpolation.discontinuity(*this, handle);
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
            apply_parent_transform(
                handle, *parent_world, world.world_matrix, world.pose_world_matrix);
        }
        world.pose_world_matrix[3] = world.world_matrix[3];
        world.revision = ++m_transform_revision;
    }
    void Scene::apply_parent_transform(entt::entity child, const WorldTransformComponent& parent,
        Math::Mat4& model, Math::Mat4& pose) const {
        const auto* attachment = m_registry.try_get<BoneAttachmentComponent>(child);
        if(attachment && !attachment->bone.empty()) {
            const auto id = m_registry.get<RelationshipComponent>(child).parent;
            const auto handle = m_entities_by_id.at(id);
            const auto source = m_animation_sources.find(handle);
            if(source != m_animation_sources.end() && source->second.bind_pose) {
                const auto& data = *source->second.data;
                const auto frame = m_animation_playbacks.find(handle);
                const auto& sample = frame != m_animation_playbacks.end() && frame->second->pose
                                         ? frame->second->pose
                                         : source->second.bind_pose;
                for(std::size_t i = 0; i < data.joints.size(); ++i) {
                    if(data.nodes[data.joints[i].node].name != attachment->bone)
                        continue;
                    const auto& bone = sample->bone_matrices[i];
                    if(const auto bone_pose = Math::without_scale(bone)) {
                        model = bone * model;
                        pose = *bone_pose * pose;
                    }
                    break;
                }
            }
        }
        model = parent.world_matrix * model;
        pose = parent.pose_world_matrix * pose;
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
        for(auto it = m_transform_work.rbegin(); it != m_transform_work.rend(); ++it) {
            update_world_transform(*it);
            m_dirty_transforms.erase(*it);
        }
        return m_transform_work.size();
    }

    std::size_t Scene::update_world_transforms() {
        if(m_children_by_parent.empty()) {
            const auto updated = m_dirty_transforms.size();
            for(const auto handle : m_dirty_transforms)
                update_world_transform(handle);
            m_dirty_transforms.clear();
            return updated;
        }
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
    Math::Mat4 Scene::get_parent_matrix(Entity entity) {
        const auto parent = get_parent(entity);
        if(!parent)
            return Math::Mat4(1);
        sync_transform_chain(parent.m_handle);
        Math::Mat4 model(1), pose(1);
        apply_parent_transform(
            entity.m_handle, m_registry.get<WorldTransformComponent>(parent.m_handle), model, pose);
        return model;
    }

    const WorldTransformComponent& Scene::get_render_transform(const Entity entity) {
        if(!is_valid(entity))
            LOG_FATAL("Cannot get render transform for an invalid entity");
        sync_transform_chain(entity.m_handle);
        return m_transform_interpolation.world(*this, entity.m_handle);
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
