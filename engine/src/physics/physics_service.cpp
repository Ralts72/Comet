#include "physics/physics_service.h"
#include "common/scope_exit.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
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
        constexpr JPH::ObjectLayer MOVING_LAYER = 1;
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

        JPH::EMotionType to_motion_type(const BodyMotion motion) {
            switch(motion) {
                case BodyMotion::Dynamic:
                    return JPH::EMotionType::Dynamic;
                case BodyMotion::Kinematic:
                    return JPH::EMotionType::Kinematic;
                default:
                    return JPH::EMotionType::Static;
            }
        }

        bool same_pose(const TransformComponent& a, const TransformComponent& b) {
            return glm::all(glm::equal(a.translation, b.translation))
                   && glm::all(glm::equal(a.rotation, b.rotation));
        }

        bool same_shape(const ColliderComponent& a, const ColliderComponent& b) {
            return a.shape == b.shape && glm::all(glm::equal(a.half_extents, b.half_extents))
                   && a.radius == b.radius && a.is_trigger == b.is_trigger;
        }

        bool valid_inverse(const float value) {
            return std::isfinite(value) && value > 0 && std::isfinite(1.0f / value)
                   && 1.0f / value > 0;
        }

        Result<JPH::MassProperties, Error> calculate_mass_properties(
            const JPH::Shape& shape, const float mass) {
            auto properties = shape.GetMassProperties();
            if(!valid_inverse(properties.mMass) || !std::isfinite(mass / properties.mMass)
                || mass / properties.mMass <= 0)
                return Result<JPH::MassProperties, Error>::failure(
                    {"Shape mass cannot be scaled to the configured mass"});
            properties.ScaleToMass(mass);
            if(!valid_inverse(properties.mMass))
                return Result<JPH::MassProperties, Error>::failure({"Invalid rigid body mass"});
            for(unsigned column = 0; column < 4; ++column)
                for(unsigned row = 0; row < 4; ++row)
                    if(!std::isfinite(properties.mInertia(row, column)))
                        return Result<JPH::MassProperties, Error>::failure(
                            {"Invalid rigid body inertia"});
            // 当前 Box／Sphere 的惯性张量是对角矩阵；逆值也须可表示。
            for(unsigned axis = 0; axis < 3; ++axis)
                if(!valid_inverse(properties.mInertia(axis, axis)))
                    return Result<JPH::MassProperties, Error>::failure(
                        {"Invalid rigid body inverse inertia"});
            return Result<JPH::MassProperties, Error>::success(properties);
        }

    }

    struct PhysicsService::Impl {
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
            void drain(std::vector<Pair>& output) {
                std::lock_guard lock(mutex);
                active.swap(output);
                active.clear();
            }
            std::mutex mutex;
            std::vector<Pair> active;
        };

        struct JoltLifetime {
            JoltLifetime() { acquire_jolt(); }
            ~JoltLifetime() { release_jolt(); }
        };

        struct Body {
            EntityId entity;
            EntityUuid uuid;
            JPH::BodyID id;
            BodyMotion motion;
            float mass;
            ColliderComponent collider;
            TransformComponent last_transform;
            JPH::Quat last_physics_rotation;
            bool active_before_step = false;
        };

        struct Contact {
            EntityId first;
            EntityId second;
            EntityUuid first_uuid;
            EntityUuid second_uuid;
            bool trigger;
            bool reported = false;
        };

        Impl() : pairs(2), broad_phase(2, 2), jobs(1024) {
            pairs.EnableCollision(STATIC_LAYER, MOVING_LAYER);
            pairs.EnableCollision(MOVING_LAYER, MOVING_LAYER);
            broad_phase.MapObjectToBroadPhaseLayer(STATIC_LAYER, JPH::BroadPhaseLayer(0));
            broad_phase.MapObjectToBroadPhaseLayer(MOVING_LAYER, JPH::BroadPhaseLayer(1));
            object_filter =
                std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, 2, pairs, 2);
            world.Init(1024, 0, 1024, 1024, broad_phase, *object_filter, pairs);
            body_lookup.resize(world.GetMaxBodies());
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

        Result<void, Error> add_body(const BodyDefinition& definition) {
            const auto& transform = definition.transform;
            const auto& collider = definition.collider;
            const auto& rigid = definition.rigid;
            const auto motion = rigid.motion;
            JPH::ShapeRefC shape;
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * transform.scale;
                shape = new JPH::BoxShape(JPH::Vec3(size.x, size.y, size.z));
            } else {
                shape = new JPH::SphereShape(collider.radius * transform.scale.x);
            }
            JPH::BodyCreationSettings settings(shape.GetPtr(), to_position(transform.translation),
                to_rotation(transform.rotation), to_motion_type(motion),
                motion == BodyMotion::Static ? STATIC_LAYER : MOVING_LAYER);
            settings.mIsSensor = collider.is_trigger;
            if(motion == BodyMotion::Dynamic) {
                auto properties = calculate_mass_properties(*shape, rigid.mass);
                if(!properties)
                    return Result<void, Error>::failure(
                        {properties.error().message + ": " + definition.uuid.to_string()});
                settings.mOverrideMassProperties =
                    JPH::EOverrideMassProperties::MassAndInertiaProvided;
                settings.mMassPropertiesOverride = properties.value();
            }
            auto activation = JPH::EActivation::Activate;
            if(motion == BodyMotion::Static)
                activation = JPH::EActivation::DontActivate;
            const auto id = world.GetBodyInterface().CreateAndAddBody(settings, activation);
            if(id.IsInvalid())
                return Result<void, Error>::failure({"Physics body capacity exceeded"});
            const auto inserted = bodies.emplace(definition.uuid,
                Body{definition.entity, definition.uuid, id, motion, rigid.mass, collider,
                    transform, world.GetBodyInterface().GetRotation(id)});
            body_lookup[id.GetIndex()] = &inserted.first->second;
            if(motion == BodyMotion::Static)
                wake_nearby_bodies(id);
            return Result<void, Error>::success();
        }

        void wake_nearby_bodies(const JPH::BodyID id) {
            JPH::AABox bounds;
            {
                const JPH::BodyLockRead lock(world.GetBodyLockInterface(), id);
                if(!lock.Succeeded())
                    return;
                bounds = lock.GetBody().GetWorldSpaceBounds();
            }
            // 静态支撑或触发区变化也必须让休眠邻居重新检测接触。
            bounds.ExpandBy(
                JPH::Vec3::sReplicate(world.GetPhysicsSettings().mSpeculativeContactDistance));
            world.GetBodyInterface().ActivateBodiesInAABox(bounds, {}, {});
        }

        void remove_body(std::map<EntityUuid, Body>::iterator it) {
            auto& interface = world.GetBodyInterface();
            wake_nearby_bodies(it->second.id);
            body_lookup[it->second.id.GetIndex()] = nullptr;
            interface.RemoveBody(it->second.id);
            interface.DestroyBody(it->second.id);
            bodies.erase(it);
        }

        const Body* find_body(const JPH::BodyID id) const {
            if(id.GetIndex() >= body_lookup.size())
                return nullptr;
            const auto* body = body_lookup[id.GetIndex()];
            // Jolt 复用槽位时序号会改变，不能把旧接触绑定到新刚体。
            if(!body || body->id != id)
                return nullptr;
            return body;
        }

        Result<void, Error> update_mass(Body& body, const float mass) {
            if(body.motion == BodyMotion::Dynamic) {
                {
                    const JPH::BodyLockWrite lock(world.GetBodyLockInterface(), body.id);
                    if(!lock.Succeeded())
                        return Result<void, Error>::failure({"Cannot access mass update target"});
                    auto& target = lock.GetBody();
                    auto properties = calculate_mass_properties(*target.GetShape(), mass);
                    if(!properties)
                        return Result<void, Error>::failure(
                            {properties.error().message + ": " + body.uuid.to_string()});
                    auto* motion = target.GetMotionProperties();
                    motion->SetMassProperties(motion->GetAllowedDOFs(), properties.value());
                }
                world.GetBodyInterface().ActivateBody(body.id);
            }
            body.mass = mass;
            return Result<void, Error>::success();
        }

        Result<void, Error> synchronize_body(
            const BodyDefinition& definition, const float delta_time) {
            const auto& transform = definition.transform;
            const auto& collider = definition.collider;
            const auto& rigid = definition.rigid;
            auto it = bodies.find(definition.uuid);
            if(it != bodies.end()) {
                if(it->second.entity != definition.entity || it->second.motion != rigid.motion
                    || !same_shape(it->second.collider, collider)
                    || !glm::all(glm::equal(it->second.last_transform.scale, transform.scale))) {
                    remove_body(it);
                    return add_body(definition);
                }
                if(it->second.mass != rigid.mass)
                    if(auto updated = update_mass(it->second, rigid.mass); !updated)
                        return updated;
                if(rigid.motion == BodyMotion::Kinematic && delta_time > 0) {
                    // 目标未变也要更新速度，避免沿用上一固定步的运动。
                    world.GetBodyInterface().MoveKinematic(it->second.id,
                        to_position(transform.translation), to_rotation(transform.rotation),
                        delta_time);
                    it->second.last_transform = transform;
                } else if(!same_pose(it->second.last_transform, transform)) {
                    if(rigid.motion == BodyMotion::Static)
                        wake_nearby_bodies(it->second.id);
                    auto activation = JPH::EActivation::DontActivate;
                    if(rigid.motion != BodyMotion::Static)
                        activation = JPH::EActivation::Activate;
                    world.GetBodyInterface().SetPositionAndRotationWhenChanged(it->second.id,
                        to_position(transform.translation), to_rotation(transform.rotation),
                        activation);
                    it->second.last_physics_rotation =
                        world.GetBodyInterface().GetRotation(it->second.id);
                    if(rigid.motion == BodyMotion::Static)
                        wake_nearby_bodies(it->second.id);
                    it->second.last_transform = transform;
                }
                return Result<void, Error>::success();
            }
            return add_body(definition);
        }

        Result<void, Error> apply_impulses(const std::span<const Impulse> impulses) {
            for(const auto& request : impulses) {
                const auto found = bodies.find(request.uuid);
                if(found == bodies.end() || found->second.entity != request.entity
                    || found->second.motion != BodyMotion::Dynamic)
                    continue;
                const auto id = found->second.id;
                const JPH::Vec3 impulse(request.value.x, request.value.y, request.value.z);
                {
                    const JPH::BodyLockRead lock(world.GetBodyLockInterface(), id);
                    if(!lock.Succeeded())
                        return Result<void, Error>::failure({"Cannot access impulse target"});
                    const auto& body = lock.GetBody();
                    const auto velocity = body.GetLinearVelocity()
                                          + impulse * body.GetMotionProperties()->GetInverseMass();
                    // Jolt 在限速前求 float 长度平方，有限冲量也可能先溢出。
                    if(!std::isfinite(velocity.LengthSq()))
                        return Result<void, Error>::failure(
                            {"Impulse exceeds supported physics velocity range: "
                                + request.uuid.to_string()});
                }
                world.GetBodyInterface().AddImpulse(id, impulse);
            }
            return Result<void, Error>::success();
        }

        void publish_contacts() {
            changes.clear();
            collector.drain(frame_contacts);
            std::sort(frame_contacts.begin(), frame_contacts.end());
            frame_contacts.erase(
                std::unique(frame_contacts.begin(), frame_contacts.end()), frame_contacts.end());
            const auto kind_for = [](const Contact& contact, const bool entering) {
                if(contact.trigger) {
                    if(entering)
                        return ContactChange::Kind::TriggerEnter;
                    return ContactChange::Kind::TriggerExit;
                }
                if(entering)
                    return ContactChange::Kind::CollisionEnter;
                return ContactChange::Kind::CollisionExit;
            };
            for(const auto& pair : frame_contacts) {
                const Body* first = find_body(pair.first);
                const Body* second = find_body(pair.second);
                if(!first || !second)
                    continue;
                if(first->uuid > second->uuid)
                    std::swap(first, second);
                const auto [found, added] = tracked_contacts.try_emplace(
                    pair, Contact{first->entity, second->entity, first->uuid, second->uuid,
                              first->collider.is_trigger || second->collider.is_trigger});
                auto& contact = found->second;
                contact.reported = true;
                if(added)
                    changes.push_back({kind_for(contact, true), contact.first, contact.second,
                        contact.first_uuid, contact.second_uuid});
            }
            const auto& interface = world.GetBodyInterface();
            for(auto it = tracked_contacts.begin(); it != tracked_contacts.end();) {
                auto& [pair, contact] = *it;
                if(std::exchange(contact.reported, false)) {
                    ++it;
                    continue;
                }
                const auto* first = find_body(pair.first);
                const auto* second = find_body(pair.second);
                // Jolt 不报告休眠接触；只有两端整步未活动才能沿用上一逻辑状态。
                if(first && second && !first->active_before_step && !second->active_before_step
                    && !interface.IsActive(pair.first) && !interface.IsActive(pair.second)) {
                    ++it;
                    continue;
                }
                changes.push_back({kind_for(contact, false), contact.first, contact.second,
                    contact.first_uuid, contact.second_uuid});
                it = tracked_contacts.erase(it);
            }
            std::sort(changes.begin(), changes.end(), [](const auto& a, const auto& b) {
                const auto order = [](const ContactChange::Kind kind) {
                    return kind != ContactChange::Kind::CollisionExit
                           && kind != ContactChange::Kind::TriggerExit;
                };
                return std::tuple{a.first_uuid, a.second_uuid, order(a.kind), a.kind}
                       < std::tuple{b.first_uuid, b.second_uuid, order(b.kind), b.kind};
            });
        }

        std::vector<Pose> poses;
        std::vector<ContactChange> changes;
        JoltLifetime lifetime;
        JPH::ObjectLayerPairFilterTable pairs;
        JPH::BroadPhaseLayerInterfaceTable broad_phase;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> object_filter;
        ContactCollector collector;
        JPH::PhysicsSystem world;
        JPH::TempAllocatorMalloc allocator;
        JPH::JobSystemSingleThreaded jobs;
        std::map<EntityUuid, Body> bodies;
        std::vector<const Body*> body_lookup;
        std::vector<Pair> frame_contacts;
        std::map<Pair, Contact> tracked_contacts;
    };

    Result<void, Error> PhysicsService::step(const float delta_time) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        const ScopeExit clear([this] { m_impulses.clear(); });
        if(auto applied = m_impl->apply_impulses(m_impulses); !applied)
            return applied;
        m_impl->poses.clear();
        for(auto& [uuid, body] : m_impl->bodies)
            body.active_before_step = m_impl->world.GetBodyInterface().IsActive(body.id);
        const auto errors = m_impl->world.Update(delta_time, 1, &m_impl->allocator, &m_impl->jobs);
        if(errors != JPH::EPhysicsUpdateError::None) {
            std::string message = "Physics capacity exceeded:";
            if((errors & JPH::EPhysicsUpdateError::ManifoldCacheFull)
                != JPH::EPhysicsUpdateError::None)
                message += " manifold cache";
            if((errors & JPH::EPhysicsUpdateError::BodyPairCacheFull)
                != JPH::EPhysicsUpdateError::None)
                message += " body pair cache";
            if((errors & JPH::EPhysicsUpdateError::ContactConstraintsFull)
                != JPH::EPhysicsUpdateError::None)
                message += " contact constraints";
            return Result<void, Error>::failure({std::move(message)});
        }
        for(auto& [uuid, body] : m_impl->bodies) {
            if(body.motion != BodyMotion::Dynamic)
                continue;
            // 本步入睡仍须回写最终姿态，本步被唤醒的刚体也不能跳过。
            if(!body.active_before_step && !m_impl->world.GetBodyInterface().IsActive(body.id))
                continue;
            JPH::RVec3 position;
            JPH::Quat rotation;
            m_impl->world.GetBodyInterface().GetPositionAndRotation(body.id, position, rotation);
            auto transform = body.last_transform;
            transform.translation = Math::Vec3(position.GetX(), position.GetY(), position.GetZ());
            // 旋转未变时保留原 Euler 表示，避免无意义的转换及舍入扰动。
            if(rotation != body.last_physics_rotation && rotation != -body.last_physics_rotation)
                transform.rotation = from_rotation(rotation);
            if(!same_pose(body.last_transform, transform))
                m_impl->poses.push_back({body.entity, transform});
            body.last_transform = transform;
            body.last_physics_rotation = rotation;
        }
        m_impl->publish_contacts();
        return Result<void, Error>::success();
    }

    PhysicsService::PhysicsService() = default;
    PhysicsService::Statistics PhysicsService::get_statistics() const {
        if(!m_impl)
            return {};
        return {.bodies = m_impl->bodies.size(),
            .active_bodies = m_impl->world.GetNumActiveBodies(JPH::EBodyType::RigidBody),
            .pose_updates = m_impl->poses.size()};
    }

    PhysicsService::~PhysicsService() {
        end();
    }

    bool PhysicsService::enqueue_impulse(
        const EntityUuid uuid, const EntityId entity, const Math::Vec3 impulse) {
        if(m_impulses.size() >= MAX_IMPULSES)
            return false;
        m_impulses.push_back({uuid, entity, impulse});
        return true;
    }

    void PhysicsService::reset() noexcept {
        m_impulses.clear();
        m_impl.reset();
    }

    Result<void, Error> PhysicsService::prepare_world() {
        if(!has_binding())
            return Result<void, Error>::failure({"Physics service is inactive"});
        if(m_impl)
            return Result<void, Error>::failure({"Physics world is already active"});
        m_impl = std::make_unique<Impl>();
        return Result<void, Error>::success();
    }

    Result<void, Error> PhysicsService::synchronize_body(
        const BodyDefinition& definition, const float delta_time) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        return m_impl->synchronize_body(definition, delta_time);
    }

    void PhysicsService::remove_body(const EntityUuid uuid, const EntityId entity) {
        if(!m_impl)
            return;
        const auto found = m_impl->bodies.find(uuid);
        if(found != m_impl->bodies.end() && found->second.entity == entity)
            m_impl->remove_body(found);
    }

    std::span<const PhysicsService::Pose> PhysicsService::poses() const {
        return m_impl->poses;
    }
    std::span<const PhysicsService::ContactChange> PhysicsService::contacts() const {
        return m_impl->changes;
    }
}
