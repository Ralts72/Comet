#include "scene/systems/physics_system.h"
#include "scene/scene.h"

#include <algorithm>

namespace Comet {
    Result<void, Error> PhysicsSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        if(services.physics != &m_physics || !m_physics.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"PhysicsSystem requires its physics service in RuntimeServices"});
        if(auto ready = m_physics.prepare_world(); !ready)
            return ready;
        m_entries.reserve(scene.component_count<RigidBodyComponent>());
        Result<void, Error> result = Result<void, Error>::success();
        scene.each<const RigidBodyComponent>([&](Entity entity, const RigidBodyComponent&) {
            if(result)
                result = synchronize_body(scene, entity.m_handle);
        });
        if(result)
            scene.acknowledge_physics_changes();
        return result;
    }

    Result<void, Error> PhysicsSystem::synchronize(Scene& scene) {
        const auto batch = scene.physics_changes();
        if(batch.structure_changed) {
            std::erase_if(m_entries, [this](const Entry& entry) {
                // has_component 已检查句柄寿命；必需组件在下方统一校验。
                if(entry.entity.has_component<RigidBodyComponent>())
                    return false;
                m_physics.remove_body(entry.uuid, entry.id);
                return true;
            });
        }
        for(const auto handle : batch.entities) {
            // 销毁会移除脏项；移除刚体的实体仍可能在本批中。
            if(!scene.m_registry.all_of<RigidBodyComponent>(handle))
                continue;
            if(auto result = synchronize_body(scene, handle); !result)
                return result;
        }
        scene.acknowledge_physics_changes();
        return Result<void, Error>::success();
    }

    Result<void, Error> PhysicsSystem::synchronize_body(Scene& scene, const entt::entity handle) {
        const auto& [id, uuid, relationship] =
            scene.m_registry.get<IdComponent, UuidComponent, RelationshipComponent>(handle);
        const auto [transform, collider] =
            scene.m_registry.try_get<TransformComponent, ColliderComponent>(handle);
        if(!transform || !collider)
            return Result<void, Error>::failure(
                {"Rigid body requires Transform and Collider: " + uuid.uuid.to_string()});
        if(relationship.parent != INVALID_ENTITY_ID)
            return Result<void, Error>::failure(
                {"Parented rigid body is unsupported: " + uuid.uuid.to_string()});
        const auto synchronized = m_physics.synchronize_body({uuid.uuid, id.id, *transform,
            *collider, scene.m_registry.get<RigidBodyComponent>(handle)});
        if(!synchronized)
            return Result<void, Error>::failure(synchronized.error());
        if(synchronized.value())
            m_entries.push_back({Entity(handle, &scene), id.id, uuid.uuid});
        return Result<void, Error>::success();
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
                return Result<void, Error>::failure({"Too many physics contacts in one frame"});
        }
        return Result<void, Error>::success();
    }

    void PhysicsSystem::on_stop(Scene&, RuntimeSession&, const RuntimeServices&) noexcept {
        m_entries.clear();
    }
}
