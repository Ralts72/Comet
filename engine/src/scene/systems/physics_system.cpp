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
        return synchronize(scene, 0);
    }

    Result<void, Error> PhysicsSystem::synchronize(Scene& scene, const float delta_time) {
        std::erase_if(m_entries, [this](const Entry& entry) {
            // has_component 已检查句柄寿命；必需组件在下方统一校验。
            if(entry.entity.has_component<RigidBodyComponent>())
                return false;
            m_physics.remove_body(entry.uuid, entry.id);
            return true;
        });
        Result<void, Error> result = Result<void, Error>::success();
        std::size_t synchronized_count = 0;
        scene.each<const RigidBodyComponent, const TransformComponent, const ColliderComponent,
            const IdComponent, const UuidComponent, const RelationshipComponent>(
            [&](Entity entity, const RigidBodyComponent& rigid, const TransformComponent& transform,
                const ColliderComponent& collider, const IdComponent& id_component,
                const UuidComponent& uuid_component, const RelationshipComponent& relationship) {
                if(!result)
                    return;
                ++synchronized_count;
                const auto uuid = uuid_component.uuid;
                if(relationship.parent != INVALID_ENTITY_ID) {
                    result = Result<void, Error>::failure(
                        {"Parented rigid body is unsupported: " + uuid.to_string()});
                    return;
                }
                const auto id = id_component.id;
                const auto synchronized =
                    m_physics.synchronize_body({uuid, id, transform, collider, rigid}, delta_time);
                if(!synchronized) {
                    result = Result<void, Error>::failure(synchronized.error());
                    return;
                }
                if(synchronized.value())
                    m_entries.push_back({entity, id, uuid});
            });
        if(!result || synchronized_count == scene.component_count<RigidBodyComponent>())
            return result;
        // 完整查询不匹配时才定位缺失组件，不能静默跳过非法刚体。
        scene.each<const RigidBodyComponent, const UuidComponent>(
            [&](Entity entity, const RigidBodyComponent&, const UuidComponent& uuid) {
                if(result
                    && (!entity.has_component<TransformComponent>()
                        || !entity.has_component<ColliderComponent>()))
                    result = Result<void, Error>::failure(
                        {"Rigid body requires Transform and Collider: " + uuid.uuid.to_string()});
            });
        return result;
    }

    Result<void, Error> PhysicsSystem::fixed_update(Scene& scene, const Context& context) {
        if(!m_physics.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"Physics service is inactive or belongs to another scene"});
        const auto delta_time = static_cast<float>(context.delta_time);
        if(auto synchronized = synchronize(scene, delta_time); !synchronized)
            return synchronized;
        if(auto stepped = m_physics.step(delta_time); !stepped)
            return stepped;
        for(const auto& pose : m_physics.poses()) {
            const auto entity = scene.find_entity(pose.entity);
            if(!entity.try_set_transform(pose.transform))
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
