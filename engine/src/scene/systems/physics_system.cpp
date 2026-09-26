#include "scene/systems/physics_system.h"
#include "scene/scene.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        constexpr JPH::ObjectLayer STATIC_LAYER = 0;
        constexpr JPH::ObjectLayer DYNAMIC_LAYER = 1;
        std::mutex jolt_mutex;
        unsigned jolt_users = 0;

        void acquire_jolt() {
            std::lock_guard lock(jolt_mutex);
            if(jolt_users++ == 0) {
                JPH::RegisterDefaultAllocator();
                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
        }

        void release_jolt() {
            std::lock_guard lock(jolt_mutex);
            if(--jolt_users == 0) {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        JPH::RVec3 to_position(const Math::Vec3& value) {
            return {value.x, value.y, value.z};
        }

        JPH::Quat to_rotation(const Math::Vec3& degrees) {
            const Math::Quat rotation =
                glm::quat_cast(Math::compose_trs(Math::Vec3(0.0f), degrees, Math::Vec3(1.0f)));
            return {rotation.x, rotation.y, rotation.z, rotation.w};
        }

        Math::Vec3 from_rotation(const JPH::Quat& rotation) {
            return Math::wrap_degrees(glm::degrees(glm::eulerAngles(
                Math::Quat(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ()))));
        }

        bool same_pose(const TransformComponent& a, const TransformComponent& b) {
            return glm::all(glm::equal(a.translation, b.translation))
                   && glm::all(glm::equal(a.rotation, b.rotation));
        }

        bool same_shape(const ColliderComponent& a, const ColliderComponent& b) {
            return a.shape == b.shape && glm::all(glm::equal(a.half_extents, b.half_extents))
                   && a.radius == b.radius && a.is_trigger == b.is_trigger;
        }

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
            const auto motion = entity.get_component<RigidBodyComponent>().motion;
            if(motion != BodyMotion::Static && motion != BodyMotion::Dynamic)
                return Result<void, Error>::failure(
                    {"Unknown rigid body motion: " + entity.get_uuid().to_string()});
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
                if(!std::isfinite(collider.radius) || collider.radius <= 0
                    || transform.scale.x != transform.scale.y
                    || transform.scale.x != transform.scale.z)
                    return Result<void, Error>::failure(
                        {"Sphere collider requires a positive radius and uniform scale: "
                            + entity.get_uuid().to_string()});
            } else {
                return Result<void, Error>::failure(
                    {"Unknown collider shape: " + entity.get_uuid().to_string()});
            }
            return Result<void, Error>::success();
        }
    }

    struct PhysicsSystem::Impl {
        using Pair = std::pair<JPH::BodyID, JPH::BodyID>;

        struct ContactCollector final: JPH::ContactListener {
            void OnContactAdded(const JPH::Body& first, const JPH::Body& second,
                const JPH::ContactManifold&, JPH::ContactSettings&) override {
                record(first, second);
            }
            void OnContactPersisted(const JPH::Body& first, const JPH::Body& second,
                const JPH::ContactManifold&, JPH::ContactSettings&) override {
                record(first, second);
            }
            void record(const JPH::Body& first, const JPH::Body& second) {
                std::lock_guard lock(mutex);
                active.emplace_back(first.GetID(), second.GetID());
            }
            std::vector<Pair> take() {
                std::lock_guard lock(mutex);
                return std::exchange(active, {});
            }
            std::mutex mutex;
            std::vector<Pair> active;
        };

        struct JoltLifetime {
            JoltLifetime() { acquire_jolt(); }
            ~JoltLifetime() { release_jolt(); }
        };

        struct Body {
            Entity entity;
            JPH::BodyID id;
            BodyMotion motion;
            ColliderComponent collider;
            TransformComponent last_transform;
        };

        struct Contact {
            Entity first;
            Entity second;
            bool trigger;
        };

        Impl() : pairs(2), broad_phase(2, 2), jobs(1024) {
            pairs.EnableCollision(STATIC_LAYER, DYNAMIC_LAYER);
            pairs.EnableCollision(DYNAMIC_LAYER, DYNAMIC_LAYER);
            broad_phase.MapObjectToBroadPhaseLayer(STATIC_LAYER, JPH::BroadPhaseLayer(0));
            broad_phase.MapObjectToBroadPhaseLayer(DYNAMIC_LAYER, JPH::BroadPhaseLayer(1));
            object_filter =
                std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, 2, pairs, 2);
            world.Init(1024, 0, 1024, 1024, broad_phase, *object_filter, pairs);
            world.SetContactListener(&collector);
        }

        ~Impl() {
            auto& interface = world.GetBodyInterface();
            for(const auto& [uuid, body] : bodies) {
                interface.RemoveBody(body.id);
                interface.DestroyBody(body.id);
            }
            bodies.clear();
        }

        Result<void, Error> add_body(Scene& scene, Entity entity) {
            if(auto checked = validate_body(scene, entity); !checked)
                return checked;
            const auto& transform = entity.get_component<TransformComponent>();
            const auto& collider = entity.get_component<ColliderComponent>();
            const auto motion = entity.get_component<RigidBodyComponent>().motion;
            JPH::ShapeRefC shape;
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * transform.scale;
                shape = new JPH::BoxShape(JPH::Vec3(size.x, size.y, size.z));
            } else {
                shape = new JPH::SphereShape(collider.radius * transform.scale.x);
            }
            JPH::BodyCreationSettings settings(shape.GetPtr(),
                to_position(transform.translation), to_rotation(transform.rotation),
                motion == BodyMotion::Static ? JPH::EMotionType::Static : JPH::EMotionType::Dynamic,
                motion == BodyMotion::Static ? STATIC_LAYER : DYNAMIC_LAYER);
            settings.mIsSensor = collider.is_trigger;
            const auto id = world.GetBodyInterface().CreateAndAddBody(
                settings, motion == BodyMotion::Static ? JPH::EActivation::DontActivate
                                                       : JPH::EActivation::Activate);
            if(id.IsInvalid())
                return Result<void, Error>::failure({"Physics body capacity exceeded"});
            bodies.emplace(entity.get_uuid(), Body{entity, id, motion, collider, transform});
            return Result<void, Error>::success();
        }

        void remove_body(std::map<EntityUuid, Body>::iterator it) {
            auto& interface = world.GetBodyInterface();
            interface.RemoveBody(it->second.id);
            interface.DestroyBody(it->second.id);
            bodies.erase(it);
        }

        Result<void, Error> synchronize_body(
            Scene& scene, Entity entity, const RigidBodyComponent& rigid) {
            auto it = bodies.find(entity.get_uuid());
            if(it != bodies.end()) {
                if(auto checked = validate_body(scene, entity); !checked)
                    return checked;
                const auto& transform = entity.get_component<TransformComponent>();
                const auto& collider = entity.get_component<ColliderComponent>();
                if(it->second.entity != entity || it->second.motion != rigid.motion
                    || !same_shape(it->second.collider, collider)
                    || !glm::all(glm::equal(it->second.last_transform.scale, transform.scale))) {
                    remove_body(it);
                    return add_body(scene, entity);
                }
                if(!same_pose(it->second.last_transform, transform)) {
                    world.GetBodyInterface().SetPositionAndRotationWhenChanged(it->second.id,
                        to_position(transform.translation), to_rotation(transform.rotation),
                        rigid.motion == BodyMotion::Dynamic ? JPH::EActivation::Activate
                                                            : JPH::EActivation::DontActivate);
                    it->second.last_transform = transform;
                }
                return Result<void, Error>::success();
            }
            return add_body(scene, entity);
        }

        Result<void, Error> synchronize(Scene& scene) {
            for(auto it = bodies.begin(); it != bodies.end();) {
                const auto& body = it->second;
                if(!body.entity || !body.entity.has_component<RigidBodyComponent>()
                    || !body.entity.has_component<ColliderComponent>()
                    || !body.entity.has_component<TransformComponent>()
                    || body.entity.get_uuid() != it->first) {
                    auto removed = it++;
                    remove_body(removed);
                    continue;
                }
                ++it;
            }
            Result<void, Error> result = Result<void, Error>::success();
            scene.each<const RigidBodyComponent>(
                [&](Entity entity, const RigidBodyComponent& rigid) {
                    if(result)
                        result = synchronize_body(scene, entity, rigid);
                });
            return result;
        }

        Result<void, Error> publish_contacts(Scene& scene) {
            auto active = collector.take();
            std::sort(active.begin(), active.end());
            active.erase(std::unique(active.begin(), active.end()), active.end());
            std::map<JPH::BodyID, const Body*> by_id;
            for(const auto& [uuid, body] : bodies)
                by_id.emplace(body.id, &body);
            std::map<Pair, Contact> current;
            for(const auto& pair : active) {
                const auto first = by_id.find(pair.first);
                const auto second = by_id.find(pair.second);
                if(first == by_id.end() || second == by_id.end())
                    continue;
                Entity first_entity = first->second->entity;
                Entity second_entity = second->second->entity;
                if(first_entity.get_uuid() > second_entity.get_uuid())
                    std::swap(first_entity, second_entity);
                current.emplace(pair, Contact{first_entity, second_entity,
                                          first->second->collider.is_trigger
                                              || second->second->collider.is_trigger});
            }
            std::vector<Scene::ContactEvent> events;
            const auto kind_for = [](const Contact& contact, const bool entering) {
                if(contact.trigger) {
                    if(entering)
                        return Scene::ContactEvent::Kind::TriggerEnter;
                    return Scene::ContactEvent::Kind::TriggerExit;
                }
                if(entering)
                    return Scene::ContactEvent::Kind::CollisionEnter;
                return Scene::ContactEvent::Kind::CollisionExit;
            };
            for(const auto& [pair, contact] : previous_contacts) {
                if(!current.contains(pair) && contact.first && contact.second)
                    events.push_back({kind_for(contact, false), contact.first, contact.second});
            }
            for(const auto& [pair, contact] : current) {
                if(!previous_contacts.contains(pair))
                    events.push_back({kind_for(contact, true), contact.first, contact.second});
            }
            std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
                const auto order = [](const Scene::ContactEvent::Kind kind) {
                    return kind != Scene::ContactEvent::Kind::CollisionExit
                           && kind != Scene::ContactEvent::Kind::TriggerExit;
                };
                return std::tuple{a.first.get_uuid(), a.second.get_uuid(), order(a.kind), a.kind}
                       < std::tuple{b.first.get_uuid(), b.second.get_uuid(), order(b.kind), b.kind};
            });
            for(const auto& event : events)
                if(!scene.append_contact_event(event))
                    return Result<void, Error>::failure({"Too many physics contacts in one frame"});
            previous_contacts = std::move(current);
            return Result<void, Error>::success();
        }

        JoltLifetime lifetime;
        JPH::ObjectLayerPairFilterTable pairs;
        JPH::BroadPhaseLayerInterfaceTable broad_phase;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> object_filter;
        ContactCollector collector;
        JPH::PhysicsSystem world;
        JPH::TempAllocatorMalloc allocator;
        JPH::JobSystemSingleThreaded jobs;
        std::map<EntityUuid, Body> bodies;
        std::map<Pair, Contact> previous_contacts;
    };

    PhysicsSystem::PhysicsSystem() = default;
    PhysicsSystem::~PhysicsSystem() = default;

    Result<void, Error> PhysicsSystem::on_start(Scene& scene) {
        if(m_impl)
            return Result<void, Error>::failure({"Physics world is already active"});
        m_impl = std::make_unique<Impl>();
        return m_impl->synchronize(scene);
    }

    Result<void, Error> PhysicsSystem::fixed_update(Scene& scene, const Context& context) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        if(auto synced = m_impl->synchronize(scene); !synced)
            return synced;
        if(m_impl->world.Update(
               static_cast<float>(context.delta_time), 1, &m_impl->allocator, &m_impl->jobs)
            != JPH::EPhysicsUpdateError::None)
            return Result<void, Error>::failure({"Physics simulation failed"});
        for(auto& [uuid, body] : m_impl->bodies) {
            if(body.motion == BodyMotion::Static)
                continue;
            JPH::RVec3 position;
            JPH::Quat rotation;
            m_impl->world.GetBodyInterface().GetPositionAndRotation(body.id, position, rotation);
            auto transform = body.entity.get_component<TransformComponent>();
            transform.translation = Math::Vec3(position.GetX(), position.GetY(), position.GetZ());
            transform.rotation = from_rotation(rotation);
            if(!body.entity.try_set_transform(transform))
                return Result<void, Error>::failure({"Cannot write physics transform"});
            body.last_transform = transform;
        }
        return m_impl->publish_contacts(scene);
    }

    void PhysicsSystem::on_stop(Scene&) noexcept {
        m_impl.reset();
    }
}
