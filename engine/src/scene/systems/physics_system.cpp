#include "scene/systems/physics_system.h"
#include "scene/scene.h"
#include "diagnostics/logger.h"

#include <algorithm>

namespace Comet {
    Result<void, Error> PhysicsSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        if(services.physics != &m_physics || !m_physics.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"PhysicsSystem requires its physics service in RuntimeServices"});
        if(auto ready = m_physics.prepare_world(); !ready)
            return ready;
        m_entries.reserve(scene.component_count<RigidBodyComponent>()
                          + scene.component_count<CharacterControllerComponent>());
        Result<void, Error> result = Result<void, Error>::success();
        const auto append = [&](Entity entity, const auto&) {
            if(result) {
                const auto synchronized = synchronize_object(scene, entity);
                if(!synchronized)
                    result = Result<void, Error>::failure(synchronized.error());
            }
        };
        scene.each<const RigidBodyComponent>(append);
        scene.each<const CharacterControllerComponent>(append);
        if(result)
            result = synchronize_joints(scene);
        if(result)
            scene.acknowledge_physics_changes();
        return result;
    }

    Result<void, Error> PhysicsSystem::synchronize(Scene& scene) {
        const auto batch = scene.physics_changes();
        bool joints_changed = batch.joints_changed || batch.structure_changed;
        if(batch.structure_changed) {
            std::vector<std::pair<EntityUuid, EntityId>> removed;
            std::vector<std::pair<EntityUuid, EntityId>> removed_characters;
            std::erase_if(m_entries, [&](const Entry& entry) {
                // has_component 已检查句柄寿命；必需组件在下方统一校验。
                if(entry.character ? entry.entity.has_component<CharacterControllerComponent>()
                                   : entry.entity.has_component<RigidBodyComponent>())
                    return false;
                auto& pending = entry.character ? removed_characters : removed;
                pending.emplace_back(entry.uuid, entry.id);
                return true;
            });
            m_physics.remove_bodies(removed);
            m_physics.remove_characters(removed_characters);
        }
        for(const auto handle : batch.entities) {
            // 销毁会移除脏项；移除刚体的实体仍可能在本批中。
            if(!scene.m_registry.any_of<RigidBodyComponent, CharacterControllerComponent>(handle))
                continue;
            const auto result = synchronize_object(scene, Entity(handle, &scene));
            if(!result)
                return Result<void, Error>::failure(result.error());
            joints_changed |= scene.m_registry.all_of<RigidBodyComponent>(handle)
                              && result.value() != PhysicsService::ObjectChange::None;
        }
        const auto mesh_changes = m_physics.synchronize_meshes();
        if(!mesh_changes)
            return Result<void, Error>::failure(mesh_changes.error());
        joints_changed |= mesh_changes.value();
        if(joints_changed) {
            if(auto result = synchronize_joints(scene); !result)
                return result;
        }
        scene.acknowledge_physics_changes();
        return Result<void, Error>::success();
    }

    Result<PhysicsService::ObjectChange, Error> PhysicsSystem::synchronize_object(
        Scene& scene, Entity entity) {
        const auto handle = entity.m_handle;
        const auto& [id, uuid, relationship] =
            scene.m_registry.get<IdComponent, UuidComponent, RelationshipComponent>(handle);
        const auto* transform = scene.m_registry.try_get<TransformComponent>(handle);
        const auto* character = scene.m_registry.try_get<CharacterControllerComponent>(handle);
        const auto* collider = scene.m_registry.try_get<ColliderComponent>(handle);
        if(!character && (!transform || !collider))
            return Result<PhysicsService::ObjectChange, Error>::failure(
                {"Rigid body requires Transform and Collider: " + uuid.uuid.to_string()});
        if(!transform)
            return Result<PhysicsService::ObjectChange, Error>::failure(
                {"Physics object requires Transform: " + uuid.uuid.to_string()});
        auto world_transform = *transform;
        if(relationship.parent != INVALID_ENTITY_ID) {
            const auto* rigid = scene.m_registry.try_get<RigidBodyComponent>(handle);
            if(character || !rigid || rigid->motion != BodyMotion::Static)
                return Result<PhysicsService::ObjectChange, Error>::failure(
                    {"Parented dynamic physics object is unsupported: " + uuid.uuid.to_string()});
            const auto& matrix = scene.get_world_matrix(Entity(handle, &scene));
            const auto scale = Math::positive_trs_scale(matrix);
            if(!scale)
                return Result<PhysicsService::ObjectChange, Error>::failure(
                    {"Static collider hierarchy requires a positive world TRS without shear: "
                        + uuid.uuid.to_string()});
            auto rotation = Math::Mat3(matrix);
            for(int i = 0; i < 3; ++i)
                rotation[i] /= (*scale)[i];
            world_transform.translation = Math::Vec3(matrix[3]);
            world_transform.scale = *scale;
            world_transform.rotation =
                Math::degrees(Math::euler_angles(Math::normalize(Math::to_quat(rotation))));
        }
        if(character && scene.m_registry.all_of<RigidBodyComponent>(handle))
            return Result<PhysicsService::ObjectChange, Error>::failure(
                {"Character and rigid body cannot own the same Transform: "
                    + uuid.uuid.to_string()});
        const auto synchronized = [&] {
            if(character)
                return m_physics.synchronize_character({uuid.uuid, id.id, *transform, *character});
            return m_physics.synchronize_body({uuid.uuid, id.id, world_transform, *collider,
                scene.m_registry.get<RigidBodyComponent>(handle)});
        }();
        if(!synchronized)
            return synchronized;
        if(synchronized.value() == PhysicsService::ObjectChange::Added
            && std::ranges::find(m_entries, id.id, &Entry::id) == m_entries.end())
            m_entries.push_back({Entity(handle, &scene), id.id, uuid.uuid, character != nullptr});
        return synchronized;
    }

    Result<void, Error> PhysicsSystem::synchronize_joints(Scene& scene) {
        std::vector<PhysicsService::JointDefinition> definitions;
        definitions.reserve(scene.component_count<PointJointComponent>()
                            + scene.component_count<HingeJointComponent>()
                            + scene.component_count<SliderJointComponent>());
        const auto append = [&](Entity entity, const auto& joint) {
            const auto connected = scene.find_entity(joint.connected_body);
            definitions.push_back({entity.get_uuid(), entity.get_id(),
                connected ? connected.get_id() : INVALID_ENTITY_ID, joint});
        };
        scene.each<const PointJointComponent>(append);
        scene.each<const HingeJointComponent>(append);
        scene.each<const SliderJointComponent>(append);
        std::ranges::sort(definitions, {}, &PhysicsService::JointDefinition::key);
        return m_physics.synchronize_joints(definitions);
    }

    Result<void, Error> PhysicsSystem::fixed_update(Scene& scene, const Context& context) {
        if(!m_physics.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"Physics service is inactive or belongs to another scene"});
        const auto delta_time = static_cast<float>(context.delta_time);
        if(auto synchronized = synchronize(scene); !synchronized)
            return synchronized;
        if(auto stepped = m_physics.step(delta_time); !stepped)
            return stepped;
        for(const auto& pose : m_physics.poses()) {
            const auto entity = scene.find_entity(pose.entity);
            // 后端刚生成的姿态只通知场景／渲染，不再排入下一步的输入同步。
            if(!entity.try_set_transform(pose.transform, false))
                return Result<void, Error>::failure({"Cannot write physics transform"});
        }
        for(const auto& broken : m_physics.broken_joints()) {
            auto entity = scene.find_entity(broken.entity);
            if(!entity || entity.get_uuid() != broken.uuid)
                return Result<void, Error>::failure({"Cannot find broken joint owner"});
            const auto applied = std::visit(
                [&](const auto& joint) {
                    auto disabled = joint;
                    disabled.enabled = false;
                    return entity.try_set_component(disabled);
                },
                broken.joint);
            if(!applied)
                return Result<void, Error>::failure({"Cannot disable broken joint"});
            const auto kind = std::visit(
                [](const auto& joint) {
                    using Joint = std::decay_t<decltype(joint)>;
                    if constexpr(std::is_same_v<Joint, PointJointComponent>)
                        return "Point";
                    else if constexpr(std::is_same_v<Joint, HingeJointComponent>)
                        return "Hinge";
                    else
                        return "Slider";
                },
                broken.joint);
            LOG_INFO("{} joint broke: {} (force={} N, torque={} N.m)", kind,
                broken.uuid.to_string(), broken.force, broken.torque);
        }
        for(const auto& contact : m_physics.contacts()) {
            const auto first = scene.find_entity(contact.first);
            const auto second = scene.find_entity(contact.second);
            if(!first || !second)
                continue;
            const auto kind = [](const PhysicsService::ContactChange::Kind value)
                -> std::optional<Scene::ContactEvent::Kind> {
                using Kind = PhysicsService::ContactChange::Kind;
                switch(value) {
                    case Kind::CollisionEnter:
                        return Scene::ContactEvent::Kind::CollisionEnter;
                    case Kind::CollisionExit:
                        return Scene::ContactEvent::Kind::CollisionExit;
                    case Kind::TriggerEnter:
                        return Scene::ContactEvent::Kind::TriggerEnter;
                    case Kind::TriggerExit:
                        return Scene::ContactEvent::Kind::TriggerExit;
                }
                return std::nullopt;
            }(contact.kind);
            if(!kind)
                return Result<void, Error>::failure({"Unknown physics contact kind"});
            if(!scene.append_contact_event({*kind, first, second}))
                return Result<void, Error>::failure(
                    {"Too many physics contacts in one fixed step"});
        }
        return Result<void, Error>::success();
    }

    void PhysicsSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_entries.clear();
    }
}
