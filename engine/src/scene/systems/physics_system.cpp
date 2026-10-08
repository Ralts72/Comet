#include "scene/systems/physics_system.h"
#include "scene/scene.h"

#include <algorithm>
#include <cmath>

namespace Comet {
    namespace {
        Result<void, Error> validate_body(Scene& scene, Entity entity) {
            if(!entity.has_component<TransformComponent>()
                || !entity.has_component<ColliderComponent>())
                return Result<void, Error>::failure({"Rigid body requires Transform and Collider: "
                                                     + entity.get_uuid().to_string()});
            if(scene.get_parent(entity))
                return Result<void, Error>::failure(
                    {"Parented rigid body is unsupported: " + entity.get_uuid().to_string()});
            const auto& transform = entity.get_component<TransformComponent>();
            const auto& collider = entity.get_component<ColliderComponent>();
            const auto& rigid = entity.get_component<RigidBodyComponent>();
            const auto motion = rigid.motion;
            if(motion != BodyMotion::Static && motion != BodyMotion::Dynamic
                && motion != BodyMotion::Kinematic)
                return Result<void, Error>::failure(
                    {"Unknown rigid body motion: " + entity.get_uuid().to_string()});
            if(!std::isfinite(rigid.mass) || rigid.mass < RigidBodyComponent::MIN_MASS)
                return Result<void, Error>::failure(
                    {"Invalid rigid body mass: " + entity.get_uuid().to_string()});
            const auto valid_scale = Math::is_finite(transform.scale)
                                     && glm::all(glm::greaterThan(transform.scale, Math::Vec3(0)));
            if(!Math::is_finite(transform.translation) || !Math::is_finite(transform.rotation)
                || !valid_scale)
                return Result<void, Error>::failure(
                    {"Invalid rigid body transform: " + entity.get_uuid().to_string()});
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * transform.scale;
                if(!Math::is_finite(size) || !glm::all(glm::greaterThan(size, Math::Vec3(0))))
                    return Result<void, Error>::failure(
                        {"Invalid box collider: " + entity.get_uuid().to_string()});
            } else if(collider.shape == ColliderShape::Sphere) {
                const auto radius = collider.radius * transform.scale.x;
                if(!std::isfinite(radius) || radius <= 0 || transform.scale.x != transform.scale.y
                    || transform.scale.x != transform.scale.z)
                    return Result<void, Error>::failure(
                        {"Sphere collider requires a finite positive scaled radius and uniform scale: "
                            + entity.get_uuid().to_string()});
            } else {
                return Result<void, Error>::failure(
                    {"Unknown collider shape: " + entity.get_uuid().to_string()});
            }
            return Result<void, Error>::success();
        }
    }

    Result<void, Error> PhysicsSystem::on_start(
        Scene& scene, RuntimeSession&, const RuntimeServices& services) {
        if(services.physics != &m_physics || !m_physics.is_bound_to(scene))
            return Result<void, Error>::failure(
                {"PhysicsSystem requires its physics service in RuntimeServices"});
        if(auto ready = m_physics.prepare_world(); !ready)
            return ready;
        return synchronize(scene, 0);
    }

    Result<void, Error> PhysicsSystem::synchronize(Scene& scene, const float delta_time) {
        std::erase_if(m_entries, [this](const auto& item) {
            const Entry& entry = item.second;
            const auto entity = entry.entity;
            if(entity && entity.has_component<RigidBodyComponent>()
                && entity.has_component<ColliderComponent>()
                && entity.has_component<TransformComponent>() && entity.get_uuid() == item.first)
                return false;
            m_physics.remove_body(item.first, entry.id);
            return true;
        });
        Result<void, Error> result = Result<void, Error>::success();
        scene.each<const RigidBodyComponent>([&](Entity entity, const RigidBodyComponent& rigid) {
            if(!result)
                return;
            result = validate_body(scene, entity);
            if(!result)
                return;
            const auto uuid = entity.get_uuid();
            const auto id = entity.get_id();
            result =
                m_physics.synchronize_body({uuid, id, entity.get_component<TransformComponent>(),
                                               entity.get_component<ColliderComponent>(), rigid},
                    delta_time);
            if(result)
                m_entries.try_emplace(uuid, Entry{entity, id});
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
            if(!entity || !entity.try_set_transform(pose.transform))
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
