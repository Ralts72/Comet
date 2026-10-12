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
#include <unordered_map>
#include <utility>
#include <vector>

namespace Comet {
    namespace {
        constexpr JPH::ObjectLayer STATIC_LAYER = 0;
        constexpr JPH::ObjectLayer MOVING_LAYER = 1;
        constexpr unsigned SCRATCH_BYTES = 1024 * 1024;
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

        JPH::EMotionQuality to_motion_quality(
            const RigidBodyComponent& rigid, const ColliderComponent& collider) {
            if(rigid.continuous_collision && rigid.motion == BodyMotion::Dynamic
                && !collider.is_trigger)
                return JPH::EMotionQuality::LinearCast;
            return JPH::EMotionQuality::Discrete;
        }

        bool same_shape(const ColliderComponent& a, const ColliderComponent& b) {
            return a.shape == b.shape && glm::all(glm::equal(a.half_extents, b.half_extents))
                   && a.radius == b.radius && a.is_trigger == b.is_trigger;
        }

        bool same_mass_settings(const RigidBodyComponent& a, const RigidBodyComponent& b) {
            if(a.mass_mode != b.mass_mode)
                return false;
            if(a.mass_mode == BodyMassMode::Mass)
                return a.mass == b.mass;
            return a.density == b.density;
        }

        Result<void, Error> validate_body(const EntityUuid uuid,
            const TransformComponent& transform, const ColliderComponent& collider,
            const RigidBodyComponent& rigid) {
            const auto motion = rigid.motion;
            if(motion != BodyMotion::Static && motion != BodyMotion::Dynamic
                && motion != BodyMotion::Kinematic)
                return Result<void, Error>::failure(
                    {"Unknown rigid body motion: " + uuid.to_string()});
            if(!std::isfinite(rigid.mass) || rigid.mass < RigidBodyComponent::MIN_MASS)
                return Result<void, Error>::failure(
                    {"Invalid rigid body mass: " + uuid.to_string()});
            if(rigid.mass_mode != BodyMassMode::Mass && rigid.mass_mode != BodyMassMode::Density)
                return Result<void, Error>::failure(
                    {"Unknown rigid body mass mode: " + uuid.to_string()});
            if(!std::isfinite(rigid.density) || rigid.density < RigidBodyComponent::MIN_DENSITY)
                return Result<void, Error>::failure(
                    {"Invalid rigid body density: " + uuid.to_string()});
            if(!std::isfinite(collider.friction) || collider.friction < 0 || collider.friction > 1
                || !std::isfinite(collider.restitution) || collider.restitution < 0
                || collider.restitution > 1)
                return Result<void, Error>::failure(
                    {"Collider friction and restitution must be finite and between 0 and 1: "
                        + uuid.to_string()});
            const auto valid_scale = Math::is_finite(transform.scale)
                                     && glm::all(glm::greaterThan(transform.scale, Math::Vec3(0)));
            if(!Math::is_finite(transform.translation) || !Math::is_finite(transform.rotation)
                || !valid_scale)
                return Result<void, Error>::failure(
                    {"Invalid rigid body transform: " + uuid.to_string()});
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * transform.scale;
                if(!Math::is_finite(size) || !glm::all(glm::greaterThan(size, Math::Vec3(0))))
                    return Result<void, Error>::failure(
                        {"Invalid box collider: " + uuid.to_string()});
            } else if(collider.shape == ColliderShape::Sphere) {
                const auto radius = collider.radius * transform.scale.x;
                if(!std::isfinite(radius) || radius <= 0 || transform.scale.x != transform.scale.y
                    || transform.scale.x != transform.scale.z)
                    return Result<void, Error>::failure(
                        {"Sphere collider requires a finite positive scaled radius and uniform scale: "
                            + uuid.to_string()});
            } else {
                return Result<void, Error>::failure(
                    {"Unknown collider shape: " + uuid.to_string()});
            }
            return Result<void, Error>::success();
        }

        bool valid_inverse(const float value) {
            return std::isfinite(value) && value > 0 && std::isfinite(1.0f / value)
                   && 1.0f / value > 0;
        }

        Result<JPH::MassProperties, Error> calculate_mass_properties(
            const JPH::Shape& shape, const RigidBodyComponent& rigid) {
            auto mass = rigid.mass;
            if(rigid.mass_mode == BodyMassMode::Density)
                mass = shape.GetVolume() * rigid.density;
            if(!std::isfinite(mass) || mass < RigidBodyComponent::MIN_MASS)
                return Result<JPH::MassProperties, Error>::failure(
                    {"Calculated rigid body mass must be finite and at least 0.001 kg"});
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
            RigidBodyComponent rigid;
            ColliderComponent collider;
            TransformComponent last_transform;
            JPH::Quat last_physics_rotation;
            bool active_this_step = false;
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
            for(const auto& [entity, body] : bodies) {
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
            settings.mMotionQuality = to_motion_quality(rigid, collider);
            settings.mFriction = collider.friction;
            settings.mRestitution = collider.restitution;
            if(motion == BodyMotion::Dynamic) {
                auto properties = calculate_mass_properties(*shape, rigid);
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
            const auto inserted = bodies.emplace(
                definition.entity, Body{definition.entity, definition.uuid, id, rigid, collider,
                                       transform, world.GetBodyInterface().GetRotation(id)});
            body_lookup[id.GetIndex()] = &inserted.first->second;
            if(motion == BodyMotion::Kinematic)
                kinematic_bodies.push_back(&inserted.first->second);
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

        void remove_body(std::unordered_map<EntityId, Body>::iterator it) {
            auto& interface = world.GetBodyInterface();
            wake_nearby_bodies(it->second.id);
            body_lookup[it->second.id.GetIndex()] = nullptr;
            if(it->second.rigid.motion == BodyMotion::Kinematic)
                std::erase(kinematic_bodies, &it->second);
            interface.RemoveBody(it->second.id);
            interface.DestroyBody(it->second.id);
            bodies.erase(it);
        }

        Body* find_body(const JPH::BodyID id) {
            if(id.GetIndex() >= body_lookup.size())
                return nullptr;
            auto* body = body_lookup[id.GetIndex()];
            // Jolt 复用槽位时序号会改变，不能把旧接触绑定到新刚体。
            if(!body || body->id != id)
                return nullptr;
            return body;
        }

        Result<void, Error> update_mass(Body& body, const RigidBodyComponent& rigid) {
            if(body.rigid.motion == BodyMotion::Dynamic) {
                {
                    const JPH::BodyLockWrite lock(world.GetBodyLockInterface(), body.id);
                    if(!lock.Succeeded())
                        return Result<void, Error>::failure({"Cannot access mass update target"});
                    auto& target = lock.GetBody();
                    auto properties = calculate_mass_properties(*target.GetShape(), rigid);
                    if(!properties)
                        return Result<void, Error>::failure(
                            {properties.error().message + ": " + body.uuid.to_string()});
                    auto* motion = target.GetMotionProperties();
                    motion->SetMassProperties(motion->GetAllowedDOFs(), properties.value());
                }
                world.GetBodyInterface().ActivateBody(body.id);
            }
            return Result<void, Error>::success();
        }

        Result<bool, Error> synchronize_body(const BodyDefinition& definition) {
            const auto& transform = definition.transform;
            const auto& collider = definition.collider;
            const auto& rigid = definition.rigid;
            auto it = bodies.find(definition.entity);
            const bool added = it == bodies.end();
            const bool rebuild =
                !added
                && (it->second.uuid != definition.uuid || it->second.rigid.motion != rigid.motion
                    || !same_shape(it->second.collider, collider)
                    || !glm::all(glm::equal(it->second.last_transform.scale, transform.scale)));
            const bool rigid_changed = !added && it->second.rigid != rigid;
            const bool pose_changed = !added && !same_pose(it->second.last_transform, transform);
            const auto motion_quality = to_motion_quality(rigid, collider);
            const bool quality_changed =
                !added
                && to_motion_quality(it->second.rigid, it->second.collider) != motion_quality;
            const bool surface_changed =
                !added
                && (it->second.collider.friction != collider.friction
                    || it->second.collider.restitution != collider.restitution);
            if(added || rebuild || rigid_changed || pose_changed || surface_changed) {
                if(auto valid = validate_body(definition.uuid, transform, collider, rigid); !valid)
                    return Result<bool, Error>::failure(valid.error());
            }
            if(added || rebuild) {
                if(rebuild)
                    remove_body(it);
                if(auto created = add_body(definition); !created)
                    return Result<bool, Error>::failure(created.error());
                return Result<bool, Error>::success(added);
            }
            auto& body = it->second;
            if(surface_changed) {
                // Jolt 每步重新组合接触系数；更新表面参数保留刚体、休眠与接触缓存。
                auto& interface = world.GetBodyInterface();
                interface.SetFriction(body.id, collider.friction);
                interface.SetRestitution(body.id, collider.restitution);
                body.collider = collider;
            }
            if(quality_changed) {
                // 原地更新 Jolt 检测方式，保留速度、休眠及接触身份。
                world.GetBodyInterface().SetMotionQuality(body.id, motion_quality);
            }
            if(!same_mass_settings(body.rigid, rigid)) {
                if(auto updated = update_mass(body, rigid); !updated)
                    return Result<bool, Error>::failure(updated.error());
            }
            body.rigid = rigid;
            if(rigid.motion == BodyMotion::Kinematic) {
                if(!glm::all(glm::equal(body.last_transform.rotation, transform.rotation)))
                    body.last_physics_rotation = to_rotation(transform.rotation);
                body.last_transform = transform;
            } else if(pose_changed) {
                if(rigid.motion == BodyMotion::Static)
                    wake_nearby_bodies(body.id);
                auto activation = JPH::EActivation::DontActivate;
                if(rigid.motion != BodyMotion::Static)
                    activation = JPH::EActivation::Activate;
                world.GetBodyInterface().SetPositionAndRotationWhenChanged(body.id,
                    to_position(transform.translation), to_rotation(transform.rotation),
                    activation);
                body.last_physics_rotation = world.GetBodyInterface().GetRotation(body.id);
                if(rigid.motion == BodyMotion::Static)
                    wake_nearby_bodies(body.id);
                body.last_transform = transform;
            }
            return Result<bool, Error>::success(false);
        }

        Result<void, Error> validate_command(const JPH::Body& body, const Command& request,
            const JPH::Vec3 value, const float delta_time) {
            const auto* motion = body.GetMotionProperties();
            auto linear = body.GetLinearVelocity();
            auto angular = body.GetAngularVelocity();
            auto force = body.GetAccumulatedForce();
            auto torque = body.GetAccumulatedTorque();
            auto moment = JPH::Vec3::sZero();
            if(request.point)
                moment = JPH::Vec3(to_position(*request.point) - body.GetCenterOfMassPosition())
                             .Cross(value);
            switch(request.kind) {
                case CommandKind::Impulse:
                    linear += value * motion->GetInverseMass();
                    if(request.point)
                        angular += motion->MultiplyWorldSpaceInverseInertiaByVector(
                            body.GetRotation(), moment);
                    break;
                case CommandKind::Force:
                    force += value;
                    torque += moment;
                    break;
                case CommandKind::Torque:
                    torque += value;
                    break;
            }
            const auto acceleration = force * motion->GetInverseMass();
            const auto angular_acceleration =
                motion->MultiplyWorldSpaceInverseInertiaByVector(body.GetRotation(), torque);
            const auto next_linear = linear + delta_time * acceleration;
            const auto next_angular = angular + delta_time * angular_acceleration;
            // Jolt 先累加／积分再限速；有限输入也可能在累加或长度平方时溢出。
            if(!std::isfinite(moment.LengthSq()) || !std::isfinite(force.LengthSq())
                || !std::isfinite(torque.LengthSq()) || !std::isfinite(acceleration.LengthSq())
                || !std::isfinite(angular_acceleration.LengthSq())
                || !std::isfinite(next_linear.LengthSq())
                || !std::isfinite(next_angular.LengthSq()))
                return Result<void, Error>::failure(
                    {"Physics command exceeds supported physics velocity range: "
                        + request.uuid.to_string()});
            return Result<void, Error>::success();
        }

        Result<void, Error> apply_commands(
            const std::span<const Command> commands, const float delta_time) {
            auto& interface = world.GetBodyInterface();
            for(const auto& request : commands) {
                const auto found = bodies.find(request.entity);
                if(found == bodies.end() || found->second.uuid != request.uuid
                    || found->second.rigid.motion != BodyMotion::Dynamic)
                    continue;
                const auto id = found->second.id;
                const JPH::Vec3 value(request.value.x, request.value.y, request.value.z);
                {
                    const JPH::BodyLockRead lock(world.GetBodyLockInterface(), id);
                    if(!lock.Succeeded())
                        return Result<void, Error>::failure(
                            {"Cannot access physics command target"});
                    if(auto valid = validate_command(lock.GetBody(), request, value, delta_time);
                        !valid)
                        return valid;
                }
                switch(request.kind) {
                    case CommandKind::Impulse:
                        if(request.point)
                            interface.AddImpulse(id, value, to_position(*request.point));
                        else
                            interface.AddImpulse(id, value);
                        break;
                    case CommandKind::Force:
                        if(request.point)
                            interface.AddForce(id, value, to_position(*request.point));
                        else
                            interface.AddForce(id, value);
                        break;
                    case CommandKind::Torque:
                        interface.AddTorque(id, value);
                        break;
                }
            }
            return Result<void, Error>::success();
        }

        void move_kinematic_bodies(const float delta_time) {
            // 目标未变也更新速度，避免沿用上一固定步的运动。
            for(const auto* body : kinematic_bodies) {
                world.GetBodyInterface().MoveKinematic(body->id,
                    to_position(body->last_transform.translation), body->last_physics_rotation,
                    delta_time);
            }
        }

        void capture_active_bodies() {
            for(const auto id : step_active_bodies)
                if(auto* body = find_body(id))
                    body->active_this_step = false;
            step_active_bodies.clear();
            // 活动数组只在 Jolt Update 之外借用，跨步只保存完整 BodyID。
            const auto count = world.GetNumActiveBodies(JPH::EBodyType::RigidBody);
            const auto* active = world.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
            for(unsigned index = 0; index < count; ++index) {
                const auto id = active[index];
                if(auto* body = find_body(id)) {
                    body->active_this_step = true;
                    step_active_bodies.push_back(id);
                }
            }
        }

        void collect_poses() {
            const auto collect = [this](Body& body) {
                if(body.rigid.motion != BodyMotion::Dynamic)
                    return;
                JPH::RVec3 position;
                JPH::Quat rotation;
                // Update 已返回，主线程独占世界，姿态回读无需逐刚体加锁。
                world.GetBodyInterfaceNoLock().GetPositionAndRotation(body.id, position, rotation);
                auto transform = body.last_transform;
                transform.translation =
                    Math::Vec3(position.GetX(), position.GetY(), position.GetZ());
                // 旋转未变时保留原 Euler 表示，避免无意义的转换及舍入扰动。
                if(rotation != body.last_physics_rotation
                    && rotation != -body.last_physics_rotation)
                    transform.rotation = from_rotation(rotation);
                if(!same_pose(body.last_transform, transform))
                    poses.push_back({body.entity, transform});
                body.last_transform = transform;
                body.last_physics_rotation = rotation;
            };
            // 本步入睡仍须回写最终姿态。
            for(const auto id : step_active_bodies)
                if(auto* body = find_body(id))
                    collect(*body);
            const auto count = world.GetNumActiveBodies(JPH::EBodyType::RigidBody);
            const auto* active = world.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
            for(unsigned index = 0; index < count; ++index) {
                const auto id = active[index];
                if(auto* body = find_body(id); body && !body->active_this_step) {
                    collect(*body); // 本步被碰撞唤醒的刚体也须立即回写。
                    body->active_this_step = true;
                    step_active_bodies.push_back(id);
                }
            }
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
            for(auto it = tracked_contacts.begin(); it != tracked_contacts.end();) {
                auto& [pair, contact] = *it;
                if(std::exchange(contact.reported, false)) {
                    ++it;
                    continue;
                }
                const auto* first = find_body(pair.first);
                const auto* second = find_body(pair.second);
                // Jolt 不报告休眠接触；两端在步进前后均未活动才能沿用上一逻辑状态。
                if(first && second && !first->active_this_step && !second->active_this_step) {
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
        JPH::TempAllocatorImplWithMallocFallback allocator{SCRATCH_BYTES};
        JPH::JobSystemSingleThreaded jobs;
        std::unordered_map<EntityId, Body> bodies;
        std::vector<Body*> body_lookup;
        std::vector<JPH::BodyID> step_active_bodies;
        std::vector<Pair> frame_contacts;
        std::map<Pair, Contact> tracked_contacts;
        // unordered_map 的重哈希不移动 Body；移除时先从该列表摘除。
        std::vector<Body*> kinematic_bodies;
    };

    Result<void, Error> PhysicsService::step(const float delta_time) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        const ScopeExit clear([this] { m_commands.clear(); });
        m_impl->move_kinematic_bodies(delta_time);
        if(auto applied = m_impl->apply_commands(m_commands, delta_time); !applied)
            return applied;
        m_impl->poses.clear();
        m_impl->capture_active_bodies();
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
        m_impl->collect_poses();
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

    bool PhysicsService::enqueue_command(const EntityUuid uuid, const EntityId entity,
        const CommandKind kind, const Math::Vec3 value, const std::optional<Math::Vec3> point) {
        if(m_commands.size() >= MAX_COMMANDS)
            return false;
        m_commands.push_back({uuid, entity, kind, value, point});
        return true;
    }

    std::optional<PhysicsAccess::Velocity> PhysicsService::read_velocity(
        const EntityUuid uuid, const EntityId entity) const {
        if(!m_impl)
            return std::nullopt;
        const auto found = m_impl->bodies.find(entity);
        if(found == m_impl->bodies.end() || found->second.uuid != uuid)
            return std::nullopt;
        JPH::Vec3 linear, angular;
        // 玩法调用与 Jolt Update 串行；返回拥有数据的值，不借用后端对象。
        m_impl->world.GetBodyInterfaceNoLock().GetLinearAndAngularVelocity(
            found->second.id, linear, angular);
        return Velocity{{linear.GetX(), linear.GetY(), linear.GetZ()},
            {angular.GetX(), angular.GetY(), angular.GetZ()}};
    }

    void PhysicsService::reset() noexcept {
        m_commands.clear();
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

    Result<bool, Error> PhysicsService::synchronize_body(const BodyDefinition& definition) {
        if(!m_impl)
            return Result<bool, Error>::failure({"Physics world is not active"});
        return m_impl->synchronize_body(definition);
    }

    void PhysicsService::remove_body(const EntityUuid uuid, const EntityId entity) {
        if(!m_impl)
            return;
        const auto found = m_impl->bodies.find(entity);
        if(found != m_impl->bodies.end() && found->second.uuid == uuid)
            m_impl->remove_body(found);
    }

    std::span<const PhysicsService::Pose> PhysicsService::poses() const {
        return m_impl->poses;
    }
    std::span<const PhysicsService::ContactChange> PhysicsService::contacts() const {
        return m_impl->changes;
    }
}
