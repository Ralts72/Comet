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
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionGroup.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>

#include <algorithm>
#include <array>
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

        struct JoltLifetime {
            JoltLifetime() { acquire_jolt(); }
            ~JoltLifetime() { release_jolt(); }
        };

        JPH::RVec3 to_position(const Math::Vec3& value) {
            return {value.x, value.y, value.z};
        }

        bool character_grounded(const JPH::CharacterVirtual& controller) {
            return controller.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround
                   && controller.GetLinearVelocity().GetY() - controller.GetGroundVelocity().GetY()
                          < 0.1f;
        }

        JPH::RefConst<JPH::Shape> character_shape(const float height, const float radius) {
            JPH::RefConst<JPH::Shape> shape;
            if(height > radius * 2)
                shape = new JPH::CapsuleShape(height * 0.5f - radius, radius);
            else
                shape = new JPH::SphereShape(radius);
            return new JPH::RotatedTranslatedShape(
                JPH::Vec3(0, height * 0.5f, 0), JPH::Quat::sIdentity(), shape);
        }

        class LayerGroupFilter final: public JPH::GroupFilter {
        public:
            bool CanCollide(const JPH::CollisionGroup& first,
                const JPH::CollisionGroup& second) const override {
                return (first.GetGroupID() & second.GetSubGroupID()) != 0
                       && (second.GetGroupID() & first.GetSubGroupID()) != 0;
            }
        };

        class LayerBodyFilter final: public JPH::BodyFilter {
        public:
            explicit LayerBodyFilter(JPH::CollisionGroup group) : m_group(std::move(group)) {}
            bool ShouldCollideLocked(const JPH::Body& body) const override {
                return m_group.CanCollide(body.GetCollisionGroup());
            }

        private:
            JPH::CollisionGroup m_group;
        };

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
                   && a.radius == b.radius && a.height == b.height && a.is_trigger == b.is_trigger
                   && a.collision_layer == b.collision_layer && a.collision_mask == b.collision_mask
                   && a.mesh == b.mesh;
        }

        bool same_mass_settings(const RigidBodyComponent& a, const RigidBodyComponent& b) {
            if(a.mass_mode != b.mass_mode)
                return false;
            if(a.mass_mode == BodyMassMode::Mass)
                return a.mass == b.mass;
            return a.density == b.density;
        }

        Result<void, Error> validate_collider_geometry(
            const TransformComponent& transform, const ColliderComponent& collider) {
            const auto valid_scale = Math::is_finite(transform.scale)
                                     && glm::all(glm::greaterThan(transform.scale, Math::Vec3(0)));
            if(!Math::is_finite(transform.translation) || !Math::is_finite(transform.rotation)
                || !valid_scale)
                return Result<void, Error>::failure({"Invalid rigid body transform"});
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * transform.scale;
                if(!Math::is_finite(size) || !glm::all(glm::greaterThan(size, Math::Vec3(0))))
                    return Result<void, Error>::failure({"Invalid box collider"});
            } else if(collider.shape == ColliderShape::Sphere
                      || collider.shape == ColliderShape::Capsule) {
                const auto radius = collider.radius * transform.scale.x;
                if(!std::isfinite(radius) || radius <= 0 || transform.scale.x != transform.scale.y
                    || transform.scale.x != transform.scale.z)
                    return Result<void, Error>::failure(
                        {"Sphere/capsule collider requires a finite positive scaled radius and uniform scale"});
                if(collider.shape == ColliderShape::Capsule) {
                    const auto height = collider.height * transform.scale.x;
                    if(!std::isfinite(height) || height <= radius * 2)
                        return Result<void, Error>::failure(
                            {"Capsule height must exceed its diameter"});
                }
            } else if(collider.uses_mesh()) {
                if(!collider.mesh)
                    return Result<void, Error>::failure({"Mesh collider requires a mesh asset"});
            } else {
                return Result<void, Error>::failure({"Unknown collider shape"});
            }
            return Result<void, Error>::success();
        }
        JPH::ShapeRefC create_collider_shape(
            const ColliderComponent& collider, const Math::Vec3 scale) {
            if(collider.shape == ColliderShape::Box) {
                const auto size = collider.half_extents * scale;
                return new JPH::BoxShape(JPH::Vec3(size.x, size.y, size.z));
            }
            if(collider.shape == ColliderShape::Capsule)
                return new JPH::CapsuleShape((collider.height * 0.5f - collider.radius) * scale.x,
                    collider.radius * scale.x);
            return new JPH::SphereShape(collider.radius * scale.x);
        }

        Result<JPH::ShapeRefC, Error> create_mesh_shape(
            ColliderShape kind, const MeshData& data, Math::Vec3 scale) {
            using ShapeResult = Result<JPH::ShapeRefC, Error>;
            if(kind != ColliderShape::ConvexHull && kind != ColliderShape::TriangleMesh)
                return ShapeResult::failure({"Expected a mesh collider shape"});
            if(!Math::is_finite(scale) || !glm::all(glm::greaterThan(scale, Math::Vec3(0))))
                return ShapeResult::failure({"Mesh collider requires finite positive scale"});
            if(data.vertices.empty() || data.indices.empty() || data.indices.size() % 3 != 0)
                return ShapeResult::failure({"Collision mesh requires indexed triangles"});
            JPH::VertexList vertices;
            vertices.reserve(data.vertices.size());
            for(const auto& vertex : data.vertices) {
                const auto position = vertex.position * scale;
                if(!Math::is_finite(position))
                    return ShapeResult::failure({"Collision mesh has nonfinite scaled vertices"});
                vertices.emplace_back(position.x, position.y, position.z);
            }
            for(const auto index : data.indices)
                if(index >= vertices.size())
                    return ShapeResult::failure({"Collision mesh index exceeds vertex count"});
            const auto baked = [&]() -> JPH::Shape::ShapeResult {
                if(kind == ColliderShape::ConvexHull) {
                    JPH::Array<JPH::Vec3> points;
                    points.reserve(data.indices.size());
                    for(const auto index : data.indices)
                        points.push_back(
                            JPH::Vec3(vertices[index].x, vertices[index].y, vertices[index].z));
                    return JPH::ConvexHullShapeSettings(points).Create();
                }
                JPH::IndexedTriangleList triangles;
                triangles.reserve(data.indices.size() / 3);
                for(std::size_t index = 0; index < data.indices.size(); index += 3)
                    triangles.emplace_back(
                        data.indices[index], data.indices[index + 1], data.indices[index + 2], 0);
                return JPH::MeshShapeSettings(std::move(vertices), std::move(triangles)).Create();
            }();
            if(baked.HasError())
                return ShapeResult::failure(
                    {"Cannot build collision mesh: " + std::string(baked.GetError().c_str())});
            JPH::ShapeRefC shape = baked.Get();
            if(kind == ColliderShape::ConvexHull
                && (!std::isfinite(shape->GetVolume()) || shape->GetVolume() <= 0))
                return ShapeResult::failure({"Convex hull requires a nonzero volume"});
            return ShapeResult::success(std::move(shape));
        }

        Result<void, Error> validate_body(const EntityUuid uuid,
            const TransformComponent& transform, const ColliderComponent& collider,
            const RigidBodyComponent& rigid) {
            const auto motion = rigid.motion;
            if(collider.shape == ColliderShape::TriangleMesh && motion != BodyMotion::Static)
                return Result<void, Error>::failure(
                    {"Triangle mesh collider requires a static rigid body: " + uuid.to_string()});
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
            auto geometry = validate_collider_geometry(transform, collider);
            if(!geometry)
                return Result<void, Error>::failure(
                    {geometry.error().message + ": " + uuid.to_string()});
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
            // 主对角惯量必须为有限正值；凸包可包含非零交叉项。
            for(unsigned axis = 0; axis < 3; ++axis)
                if(!valid_inverse(properties.mInertia(axis, axis)))
                    return Result<JPH::MassProperties, Error>::failure(
                        {"Invalid rigid body inverse inertia"});
            return Result<JPH::MassProperties, Error>::success(properties);
        }

        template<typename Settings>
        Result<void, Error> validate_joint(const Settings& settings, const EntityUuid uuid) {
            const auto fail = [&](const char* message) {
                const char* kind = "Point";
                if constexpr(std::is_same_v<Settings, HingeJointComponent>)
                    kind = "Hinge";
                else if constexpr(std::is_same_v<Settings, SliderJointComponent>)
                    kind = "Slider";
                return Result<void, Error>::failure(
                    {std::string(kind) + " " + message + ": " + uuid.to_string()});
            };
            if(settings.connected_body == uuid)
                return fail("joint cannot connect a body to itself");
            if(!Math::is_finite(settings.anchor) || !Math::is_finite(settings.connected_anchor))
                return fail("anchors must be finite");
            if(!std::isfinite(settings.break_force) || settings.break_force < 0)
                return fail("break force must be finite and nonnegative");
            if constexpr(!std::is_same_v<Settings, PointJointComponent>) {
                if(!std::isfinite(settings.break_torque) || settings.break_torque < 0)
                    return fail("break torque must be finite and nonnegative");
                if(!Math::normalized_direction(settings.axis)
                    || !Math::normalized_direction(settings.connected_axis))
                    return fail("axes must be finite and nonzero");
                const bool needs_reference =
                    std::is_same_v<Settings, SliderJointComponent> || settings.limits_enabled
                    || (settings.motor_enabled && settings.motor_mode == JointMotorMode::Position);
                if(needs_reference
                    && (!Math::perpendicular_direction(settings.axis, settings.reference_axis)
                        || !Math::perpendicular_direction(
                            settings.connected_axis, settings.connected_reference_axis)))
                    return fail("references must be finite and not parallel to their axes");
                if constexpr(std::is_same_v<Settings, HingeJointComponent>) {
                    if(settings.limits_enabled && !settings.has_valid_angle_range())
                        return fail("limits must span zero within [-180, 180]");
                } else {
                    if(settings.limits_enabled && !settings.has_valid_distance_range())
                        return fail("limits must span zero with finite distances");
                }
                if(settings.limits_enabled) {
                    if(!std::isfinite(settings.limits_frequency) || settings.limits_frequency < 0)
                        return fail("limit frequency must be finite and nonnegative");
                    if(settings.limits_frequency > 0
                        && (!std::isfinite(settings.limits_damping) || settings.limits_damping < 0))
                        return fail("limit damping must be finite and nonnegative");
                }
                if(settings.motor_enabled) {
                    float capacity;
                    if constexpr(std::is_same_v<Settings, HingeJointComponent>)
                        capacity = settings.motor_max_torque;
                    else
                        capacity = settings.motor_max_force;
                    if(!std::isfinite(capacity) || capacity < 0)
                        return fail("motor capacity must be finite and nonnegative");
                    switch(settings.motor_mode) {
                        case JointMotorMode::Velocity:
                            if(!std::isfinite(settings.motor_speed))
                                return fail("motor speed must be finite");
                            break;
                        case JointMotorMode::Position:
                            if(!std::isfinite(settings.motor_target)
                                || !std::isfinite(settings.motor_frequency)
                                || settings.motor_frequency < 0.001f
                                || !std::isfinite(settings.motor_damping)
                                || settings.motor_damping < 0)
                                return fail(
                                    "position motor requires finite target, positive frequency and nonnegative damping");
                            if constexpr(std::is_same_v<Settings, HingeJointComponent>)
                                if(settings.motor_target < -180 || settings.motor_target > 180)
                                    return fail("motor target angle must be within [-180, 180]");
                            break;
                        default:
                            return fail("unknown motor mode");
                    }
                }
            }
            return Result<void, Error>::success();
        }

        template<typename Settings, typename Native>
        void configure_axis_joint(const Settings& settings, Native& native) {
            const auto first = *Math::normalized_direction(settings.axis);
            const auto second = *Math::normalized_direction(settings.connected_axis);
            const JPH::Vec3 first_axis{first.x, first.y, first.z};
            const JPH::Vec3 second_axis{second.x, second.y, second.z};
            if constexpr(std::is_same_v<Settings, HingeJointComponent>) {
                native.mHingeAxis1 = first_axis;
                native.mHingeAxis2 = second_axis;
            } else {
                native.mSliderAxis1 = first_axis;
                native.mSliderAxis2 = second_axis;
            }
            native.mNormalAxis1 = first_axis.GetNormalizedPerpendicular();
            native.mNormalAxis2 = second_axis.GetNormalizedPerpendicular();
            if(std::is_same_v<Settings, SliderJointComponent> || settings.limits_enabled
                || (settings.motor_enabled && settings.motor_mode == JointMotorMode::Position)) {
                const auto first_reference =
                    *Math::perpendicular_direction(settings.axis, settings.reference_axis);
                const auto second_reference = *Math::perpendicular_direction(
                    settings.connected_axis, settings.connected_reference_axis);
                native.mNormalAxis1 = {first_reference.x, first_reference.y, first_reference.z};
                native.mNormalAxis2 = {second_reference.x, second_reference.y, second_reference.z};
            }
            if(settings.limits_enabled && settings.limits_frequency > 0)
                native.mLimitsSpringSettings = {JPH::ESpringMode::FrequencyAndDamping,
                    settings.limits_frequency, settings.limits_damping};
            if(settings.motor_enabled && settings.motor_mode == JointMotorMode::Position)
                native.mMotorSettings.mSpringSettings = {JPH::ESpringMode::FrequencyAndDamping,
                    settings.motor_frequency, settings.motor_damping};
        }

        template<typename Settings, typename Native>
        void configure_joint_motor(const Settings& settings, Native& native) {
            if(!settings.motor_enabled)
                return;
            if(settings.motor_mode == JointMotorMode::Velocity) {
                if constexpr(std::is_same_v<Settings, HingeJointComponent>)
                    native.SetTargetAngularVelocity(-Math::radians(settings.motor_speed));
                else
                    native.SetTargetVelocity(-settings.motor_speed);
                native.SetMotorState(JPH::EMotorState::Velocity);
            } else {
                if constexpr(std::is_same_v<Settings, HingeJointComponent>)
                    native.SetTargetAngle(-Math::radians(settings.motor_target));
                else
                    native.SetTargetPosition(-settings.motor_target);
                native.SetMotorState(JPH::EMotorState::Position);
            }
        }

        template<typename Settings> bool is_breakable(const Settings& settings) {
            if constexpr(std::is_same_v<Settings, PointJointComponent>)
                return settings.break_force > 0;
            else
                return settings.break_force > 0 || settings.break_torque > 0;
        }

        template<typename Settings>
        std::pair<float, float> joint_impulses(const JPH::TwoBodyConstraint& constraint) {
            if constexpr(std::is_same_v<Settings, PointJointComponent>) {
                const auto& point = static_cast<const JPH::PointConstraint&>(constraint);
                return {point.GetTotalLambdaPosition().Length(), 0};
            } else if constexpr(std::is_same_v<Settings, HingeJointComponent>) {
                const auto& hinge = static_cast<const JPH::HingeConstraint&>(constraint);
                const auto rotation = hinge.GetTotalLambdaRotation();
                // 限位与驱动可能方向相反；断裂按较大载荷判断，不让它们抵消。
                const auto axial = std::max(std::abs(hinge.GetTotalLambdaRotationLimits()),
                    std::abs(hinge.GetTotalLambdaMotor()));
                return {hinge.GetTotalLambdaPosition().Length(),
                    std::hypot(rotation[0], rotation[1], axial)};
            } else {
                const auto& slider = static_cast<const JPH::SliderConstraint&>(constraint);
                const auto position = slider.GetTotalLambdaPosition();
                const auto axial = std::max(std::abs(slider.GetTotalLambdaPositionLimits()),
                    std::abs(slider.GetTotalLambdaMotor()));
                return {std::hypot(position[0], position[1], axial),
                    slider.GetTotalLambdaRotation().Length()};
            }
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

        struct Body {
            EntityId entity;
            EntityUuid uuid;
            JPH::BodyID id;
            RigidBodyComponent rigid;
            ColliderComponent collider;
            TransformComponent last_transform;
            JPH::Quat last_physics_rotation;
            bool active_this_step = false;
            AssetRevision mesh_revision = 0;
        };

        struct Contact {
            EntityId first;
            EntityId second;
            EntityUuid first_uuid;
            EntityUuid second_uuid;
            bool trigger;
            bool reported = false;
        };

        struct Character {
            EntityId entity;
            EntityUuid uuid;
            CharacterControllerComponent settings;
            TransformComponent last_transform;
            JPH::Ref<JPH::CharacterVirtual> controller;
            JPH::RefConst<JPH::Shape> standing_shape;
            JPH::RefConst<JPH::Shape> crouching_shape;
            Math::Vec3 relative_velocity{0};
            bool crouched = false;
            bool running = false;
        };

        struct ContactBody {
            EntityId entity;
            EntityUuid uuid;
            bool trigger;
            bool active_this_step;
            uint32_t layer;
        };

        struct Joint {
            EntityUuid uuid;
            JPH::BodyID first;
            JPH::BodyID second;
            JointSettings settings;
            JPH::Ref<JPH::TwoBodyConstraint> constraint;
        };
        using JointMap = std::map<JointKey, Joint>;

        explicit Impl(const MeshSource& source)
            : mesh_source(source), pairs(2), broad_phase(2, 2), jobs(1024) {
            pairs.EnableCollision(STATIC_LAYER, MOVING_LAYER);
            pairs.EnableCollision(MOVING_LAYER, MOVING_LAYER);
            broad_phase.MapObjectToBroadPhaseLayer(STATIC_LAYER, JPH::BroadPhaseLayer(0));
            broad_phase.MapObjectToBroadPhaseLayer(MOVING_LAYER, JPH::BroadPhaseLayer(1));
            object_filter =
                std::make_unique<JPH::ObjectVsBroadPhaseLayerFilterTable>(broad_phase, 2, pairs, 2);
            world.Init(1024, 0, 1024, 1024, broad_phase, *object_filter, pairs);
            body_lookup.resize(world.GetMaxBodies());
            character_lookup.resize(world.GetMaxBodies());
            world.SetContactListener(&collector);
        }

        ~Impl() {
            // 角色销毁其代理刚体，此时物理世界与支撑刚体仍有效。
            characters.clear();
            // Jolt 关节借用 Body，必须先从世界移除并释放，再销毁刚体。
            for(const auto& [entity, joint] : joints)
                world.RemoveConstraint(joint.constraint.GetPtr());
            breakable_joints.clear();
            joints.clear();
            auto& interface = world.GetBodyInterface();
            for(const auto& [entity, body] : bodies) {
                interface.RemoveBody(body.id);
                interface.DestroyBody(body.id);
            }
            bodies.clear();
        }

        AssetRevision mesh_revision(AssetHandle handle) const {
            return mesh_source.revision ? mesh_source.revision(handle) : 0;
        }

        struct CachedShape {
            ColliderShape kind;
            Math::Vec3 scale;
            JPH::ShapeRefC shape;
        };
        struct CachedMesh {
            AssetRevision revision = 0;
            std::vector<CachedShape> shapes;
        };

        Result<JPH::ShapeRefC, Error> collider_shape(
            const ColliderComponent& collider, Math::Vec3 scale) {
            using ShapeResult = Result<JPH::ShapeRefC, Error>;
            if(!collider.uses_mesh())
                return ShapeResult::success(create_collider_shape(collider, scale));
            if(!mesh_source.read)
                return ShapeResult::failure({"Mesh collider requires a CPU mesh source"});
            const auto revision = mesh_revision(collider.mesh);
            auto& cached = mesh_shapes[collider.mesh];
            if(cached.revision != revision) {
                cached.shapes.clear();
                cached.revision = revision;
            }
            for(const auto& candidate : cached.shapes)
                if(candidate.kind == collider.shape && candidate.scale == scale)
                    return ShapeResult::success(candidate.shape);
            auto source = mesh_source.read(collider.mesh);
            if(!source)
                return ShapeResult::failure(source.error());
            auto created = create_mesh_shape(collider.shape, source.value(), scale);
            if(!created)
                return created;
            auto shape = std::move(created).value();
            if(mesh_revision(collider.mesh) != revision)
                return ShapeResult::failure({"Collision mesh changed during shape construction"});
            cached.shapes.push_back({collider.shape, scale, shape});
            return ShapeResult::success(std::move(shape));
        }

        Result<bool, Error> synchronize_meshes() {
            std::vector<EntityId> changed;
            for(const auto* body : mesh_bodies)
                if(body->mesh_revision != mesh_revision(body->collider.mesh))
                    changed.push_back(body->entity);
            for(const auto entity : changed) {
                // 重建会销毁旧记录；组件和姿态必须先复制，不能借用被删除的 Body。
                const auto body = bodies.at(entity);
                auto result = synchronize_body(
                    {body.uuid, body.entity, body.last_transform, body.collider, body.rigid});
                if(!result)
                    return Result<bool, Error>::failure(result.error());
            }
            return Result<bool, Error>::success(!changed.empty());
        }

        void release_unused_shapes() {
            for(auto it = mesh_shapes.begin(); it != mesh_shapes.end();) {
                std::erase_if(it->second.shapes,
                    [](const CachedShape& shape) { return shape.shape->GetRefCount() == 1; });
                if(it->second.shapes.empty())
                    it = mesh_shapes.erase(it);
                else
                    ++it;
            }
        }

        Result<void, Error> add_body(
            const BodyDefinition& definition, const Velocity& velocity = {}) {
            const auto& transform = definition.transform;
            const auto& collider = definition.collider;
            const auto& rigid = definition.rigid;
            const auto motion = rigid.motion;
            auto created = collider_shape(collider, transform.scale);
            if(!created)
                return Result<void, Error>::failure(
                    {created.error().message + ": " + definition.uuid.to_string()});
            const auto& shape = created.value();
            JPH::BodyCreationSettings settings(shape.GetPtr(), to_position(transform.translation),
                to_rotation(transform.rotation), to_motion_type(motion),
                motion == BodyMotion::Static ? STATIC_LAYER : MOVING_LAYER);
            settings.mCollisionGroup = JPH::CollisionGroup(
                layer_filter, collider.collision_layer, collider.collision_mask);
            settings.mIsSensor = collider.is_trigger;
            settings.mMotionQuality = to_motion_quality(rigid, collider);
            settings.mFriction = collider.friction;
            settings.mRestitution = collider.restitution;
            if(motion == BodyMotion::Dynamic) {
                settings.mLinearVelocity = {
                    velocity.linear.x, velocity.linear.y, velocity.linear.z};
                settings.mAngularVelocity = {
                    velocity.angular.x, velocity.angular.y, velocity.angular.z};
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
            if(collider.uses_mesh()) {
                inserted.first->second.mesh_revision = mesh_revision(collider.mesh);
                mesh_bodies.push_back(&inserted.first->second);
            }
            if(motion == BodyMotion::Kinematic)
                kinematic_bodies.push_back(&inserted.first->second);
            if(motion == BodyMotion::Static)
                wake_nearby_bodies(id);
            return Result<void, Error>::success();
        }

        void wake_joint_bodies(const Joint& joint) {
            for(const auto id : {joint.first, joint.second}) {
                const auto* body = find_body(id);
                if(body && body->rigid.motion == BodyMotion::Dynamic)
                    world.GetBodyInterface().ActivateBody(id);
            }
        }

        auto erase_joint(JointMap::iterator it) {
            world.RemoveConstraint(it->second.constraint.GetPtr());
            wake_joint_bodies(it->second);
            return joints.erase(it);
        }

        auto remove_joint(JointMap::iterator it) {
            std::erase(breakable_joints, it);
            return erase_joint(it);
        }

        void remove_joints_if(const auto& remove) {
            std::erase_if(
                breakable_joints, [&](const auto it) { return remove(it->first, it->second); });
            for(auto it = joints.begin(); it != joints.end();) {
                if(remove(it->first, it->second))
                    it = erase_joint(it);
                else
                    ++it;
            }
        }

        template<typename Settings>
        Result<void, Error> synchronize_joint(
            const JointDefinition& definition, const Settings& settings) {
            auto existing = joints.find(definition.key());
            const auto detach = [&] {
                if(existing != joints.end())
                    remove_joint(existing);
                return Result<void, Error>::success();
            };
            if(!settings.enabled)
                return detach();
            if(auto valid = validate_joint(settings, definition.uuid); !valid)
                return valid;
            const auto first = bodies.find(definition.entity);
            const auto second = bodies.find(definition.connected_entity);
            if(first == bodies.end() || first->second.uuid != definition.uuid)
                return detach();
            if(settings.connected_body
                && (second == bodies.end() || second->second.uuid != settings.connected_body))
                return detach();
            const auto& owner = first->second;
            const Body* connected = nullptr;
            if(settings.connected_body)
                connected = &second->second;
            if(owner.rigid.motion != BodyMotion::Dynamic
                && (!connected || connected->rigid.motion != BodyMotion::Dynamic))
                return Result<void, Error>::failure(
                    {"Joint requires at least one dynamic body: " + definition.uuid.to_string()});
            const auto first_point = settings.anchor * owner.last_transform.scale;
            auto second_point = settings.connected_anchor;
            JPH::BodyID second_id;
            if(connected) {
                second_point *= connected->last_transform.scale;
                second_id = connected->id;
            }
            if(!Math::is_finite(first_point) || !Math::is_finite(second_point)
                || !std::isfinite(glm::dot(first_point, first_point))
                || !std::isfinite(glm::dot(second_point, second_point)))
                return Result<void, Error>::failure(
                    {"Joint anchors exceed supported range: " + definition.uuid.to_string()});
            if(existing != joints.end() && existing->second.uuid == definition.uuid
                && existing->second.first == owner.id && existing->second.second == second_id
                && existing->second.settings == definition.joint)
                return Result<void, Error>::success();

            JPH::Ref<JPH::TwoBodyConstraint> constraint;
            const JPH::BodyID ids[]{owner.id, second_id};
            {
                const JPH::BodyLockMultiWrite lock(world.GetBodyLockInterface(), ids, 2);
                auto* first_body = lock.GetBody(0);
                auto* second_body = &JPH::Body::sFixedToWorld;
                if(connected)
                    second_body = lock.GetBody(1);
                if(!first_body || !second_body)
                    return Result<void, Error>::failure({"Cannot access joint bodies"});
                const auto create = [&](auto& native) {
                    native.mSpace = JPH::EConstraintSpace::LocalToBodyCOM;
                    native.mPoint1 = to_position(first_point);
                    native.mPoint2 = to_position(second_point);
                    constraint = native.Create(*first_body, *second_body);
                };
                if constexpr(std::is_same_v<Settings, HingeJointComponent>) {
                    JPH::HingeConstraintSettings native;
                    configure_axis_joint(settings, native);
                    if(settings.limits_enabled) {
                        // Jolt 计算连接端相对本端的角度，与内容语义符号相反。
                        native.mLimitsMin = -Math::radians(settings.max_angle);
                        native.mLimitsMax = -Math::radians(settings.min_angle);
                    }
                    if(settings.motor_enabled)
                        native.mMotorSettings.SetTorqueLimit(settings.motor_max_torque);
                    create(native);
                    configure_joint_motor(
                        settings, static_cast<JPH::HingeConstraint&>(*constraint.GetPtr()));
                } else if constexpr(std::is_same_v<Settings, SliderJointComponent>) {
                    JPH::SliderConstraintSettings native;
                    configure_axis_joint(settings, native);
                    if(settings.limits_enabled) {
                        // Jolt 位移为连接锚点减本端锚点，与内容语义符号相反。
                        native.mLimitsMin = -settings.max_distance;
                        native.mLimitsMax = -settings.min_distance;
                    }
                    if(settings.motor_enabled)
                        native.mMotorSettings.SetForceLimit(settings.motor_max_force);
                    create(native);
                    configure_joint_motor(
                        settings, static_cast<JPH::SliderConstraint&>(*constraint.GetPtr()));
                } else {
                    JPH::PointConstraintSettings native;
                    create(native);
                }
            }
            if(existing != joints.end())
                remove_joint(existing);
            Joint joint{
                definition.uuid, owner.id, second_id, definition.joint, std::move(constraint)};
            const auto inserted = joints.emplace(definition.key(), std::move(joint)).first;
            world.AddConstraint(inserted->second.constraint.GetPtr());
            if(is_breakable(settings))
                breakable_joints.push_back(inserted);
            wake_joint_bodies(inserted->second);
            return Result<void, Error>::success();
        }

        Result<void, Error> synchronize_joints(const std::span<const JointDefinition> definitions) {
            remove_joints_if([&](const JointKey& key, const Joint& joint) {
                const auto found =
                    std::ranges::lower_bound(definitions, key, {}, &JointDefinition::key);
                return found == definitions.end() || found->key() != key
                       || found->uuid != joint.uuid;
            });
            for(const auto& definition : definitions) {
                const auto result = std::visit(
                    [&](const auto& settings) { return synchronize_joint(definition, settings); },
                    definition.joint);
                if(!result)
                    return result;
            }
            return Result<void, Error>::success();
        }

        void collect_broken_joints(const float delta_time) {
            broken_joints.clear();
            for(const auto it : breakable_joints) {
                const auto& joint = it->second;
                if(!joint.constraint->IsActive())
                    continue;
                std::visit(
                    [&](const auto& settings) {
                        using Settings = std::decay_t<decltype(settings)>;
                        const auto [linear, angular] =
                            joint_impulses<Settings>(*joint.constraint.GetPtr());
                        bool broken =
                            settings.break_force > 0 && linear > settings.break_force * delta_time;
                        if constexpr(!std::is_same_v<Settings, PointJointComponent>)
                            broken |= settings.break_torque > 0
                                      && angular > settings.break_torque * delta_time;
                        if(broken)
                            broken_joints.push_back({it->first.first, joint.uuid, settings,
                                linear / delta_time, angular / delta_time});
                    },
                    joint.settings);
            }
            if(broken_joints.empty())
                return;
            const auto key = [](const JointBreak& broken) {
                return JointKey{broken.entity, broken.joint.index()};
            };
            std::ranges::sort(broken_joints, {}, key);
            // 先一次摘除索引，再释放约束；采样期间始终保留有效迭代器。
            std::erase_if(breakable_joints, [&](const auto it) {
                return std::ranges::binary_search(broken_joints, it->first, {}, key);
            });
            for(const auto& broken : broken_joints)
                erase_joint(joints.find(key(broken)));
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

        void erase_body(std::unordered_map<EntityId, Body>::iterator it) {
            if(it->second.collider.uses_mesh())
                std::erase(mesh_bodies, &it->second);
            auto& interface = world.GetBodyInterface();
            wake_nearby_bodies(it->second.id);
            body_lookup[it->second.id.GetIndex()] = nullptr;
            interface.RemoveBody(it->second.id);
            interface.DestroyBody(it->second.id);
            bodies.erase(it);
        }

        void remove_body(std::unordered_map<EntityId, Body>::iterator it) {
            const auto id = it->second.id;
            remove_joints_if([&](const JointKey&, const Joint& joint) {
                return joint.first == id || joint.second == id;
            });
            if(it->second.rigid.motion == BodyMotion::Kinematic)
                std::erase(kinematic_bodies, &it->second);
            erase_body(it);
        }

        void remove_bodies(const std::span<const std::pair<EntityUuid, EntityId>> definitions) {
            if(definitions.empty())
                return;
            std::vector<JPH::BodyID> ids;
            ids.reserve(definitions.size());
            for(const auto& [uuid, entity] : definitions) {
                const auto found = bodies.find(entity);
                if(found != bodies.end() && found->second.uuid == uuid)
                    ids.push_back(found->second.id);
            }
            if(ids.empty())
                return;
            std::sort(ids.begin(), ids.end());
            const auto removing = [&](const JPH::BodyID id) {
                return std::binary_search(ids.begin(), ids.end(), id);
            };
            remove_joints_if([&](const JointKey&, const Joint& joint) {
                return removing(joint.first) || removing(joint.second);
            });
            std::erase_if(kinematic_bodies, [&](const Body* body) { return removing(body->id); });
            for(const auto& [uuid, entity] : definitions) {
                const auto found = bodies.find(entity);
                if(found != bodies.end() && found->second.uuid == uuid)
                    erase_body(found);
            }
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

        std::optional<ContactBody> find_contact_body(const JPH::BodyID id) {
            if(const auto* body = find_body(id))
                return ContactBody{body->entity, body->uuid, body->collider.is_trigger,
                    body->active_this_step, body->collider.collision_layer};
            if(id.GetIndex() >= character_lookup.size())
                return std::nullopt;
            const auto* character = character_lookup[id.GetIndex()];
            if(!character || character->controller->GetInnerBodyID() != id)
                return std::nullopt;
            return ContactBody{character->entity, character->uuid, false, true,
                character->settings.collision_layer};
        }

        class QueryBodyFilter final: public JPH::BodyFilter {
        public:
            QueryBodyFilter(Impl& impl, const QueryFilter& filter)
                : m_impl(impl), m_filter(filter) {}
            bool ShouldCollide(const JPH::BodyID& id) const override {
                const auto owner = m_impl.find_contact_body(id);
                return owner && owner->uuid != m_filter.ignored
                       && (owner->layer & m_filter.layers) != 0;
            }
            bool ShouldCollideLocked(const JPH::Body& body) const override {
                return m_filter.include_triggers || !body.IsSensor();
            }

        private:
            Impl& m_impl;
            const QueryFilter& m_filter;
        };

        auto erase_character(std::map<EntityId, Character>::iterator it) {
            character_lookup[it->second.controller->GetInnerBodyID().GetIndex()] = nullptr;
            return characters.erase(it);
        }

        void remove_characters(const std::span<const std::pair<EntityUuid, EntityId>> definitions) {
            for(const auto& [uuid, entity] : definitions) {
                const auto it = characters.find(entity);
                if(it != characters.end() && it->second.uuid == uuid)
                    erase_character(it);
            }
        }

        Result<ObjectChange, Error> synchronize_character(const CharacterDefinition& definition) {
            const auto& settings = definition.settings;
            const auto& transform = definition.transform;
            const JPH::CollisionGroup group(
                layer_filter, settings.collision_layer, settings.collision_mask);
            const LayerBodyFilter filter(group);
            auto it = characters.find(definition.entity);
            if(!settings.enabled) {
                if(it != characters.end())
                    erase_character(it);
                return Result<ObjectChange, Error>::success(ObjectChange::None);
            }
            const auto scale = transform.scale.x;
            const auto radius = settings.radius * scale;
            const auto height = settings.height * scale;
            if(!Math::is_finite(transform.translation) || !Math::is_finite(transform.rotation)
                || !Math::is_finite(transform.scale) || scale <= 0
                || transform.scale != Math::Vec3(scale) || transform.rotation.x != 0
                || transform.rotation.z != 0 || !std::isfinite(settings.radius)
                || settings.radius < 0.01f || !std::isfinite(settings.height)
                || settings.height <= settings.radius * 2 || !std::isfinite(height) || radius <= 0
                || height <= radius * 2 || !std::isfinite(settings.max_slope_angle)
                || settings.max_slope_angle < 1 || settings.max_slope_angle > 89)
                return Result<ObjectChange, Error>::failure(
                    {"Character needs an upright capsule, positive uniform scale and height > diameter: "
                        + definition.uuid.to_string()});
            for(const auto value : {settings.move_speed, settings.run_speed, settings.crouch_speed,
                    settings.push_force, settings.jump_speed, settings.step_height,
                    settings.ground_snap_distance, settings.gravity_scale})
                if(!std::isfinite(value) || value < 0 || !std::isfinite(value * scale))
                    return Result<ObjectChange, Error>::failure(
                        {"Character movement settings must be finite and nonnegative: "
                            + definition.uuid.to_string()});
            if(!std::isfinite(settings.crouch_height_ratio) || settings.crouch_height_ratio < 0.1f
                || settings.crouch_height_ratio > 1 || !std::isfinite(settings.air_control)
                || settings.air_control < 0 || settings.air_control > 1)
                return Result<ObjectChange, Error>::failure(
                    {"Character crouch ratio or air control is outside its range"});

            const bool added = it == characters.end();
            const bool rebuild =
                !added
                && (it->second.uuid != definition.uuid
                    || it->second.settings.radius != settings.radius
                    || it->second.settings.height != settings.height
                    || it->second.settings.crouch_height_ratio != settings.crouch_height_ratio
                    || it->second.settings.collision_layer != settings.collision_layer
                    || it->second.settings.collision_mask != settings.collision_mask
                    || it->second.last_transform.scale != transform.scale);
            auto velocity = JPH::Vec3::sZero();
            bool crouched = false;
            if(rebuild) {
                if(it->second.uuid == definition.uuid) {
                    velocity = it->second.controller->GetLinearVelocity();
                    crouched = it->second.crouched;
                }
                erase_character(it);
            }
            if(added || rebuild) {
                const auto standing = character_shape(height, radius);
                const auto crouching = character_shape(
                    std::max(radius * 2, height * settings.crouch_height_ratio), radius);
                JPH::CharacterVirtualSettings native;
                native.mShape = crouched ? crouching : standing;
                native.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
                native.mMaxSlopeAngle = Math::radians(settings.max_slope_angle);
                native.mMaxStrength = settings.push_force;
                native.mInnerBodyShape = native.mShape;
                native.mInnerBodyLayer = MOVING_LAYER;
                JPH::Ref<JPH::CharacterVirtual> controller = new JPH::CharacterVirtual(&native,
                    to_position(transform.translation), to_rotation(transform.rotation), &world);
                if(controller->GetInnerBodyID().IsInvalid())
                    return Result<ObjectChange, Error>::failure(
                        {"Physics character capacity exceeded"});
                world.GetBodyInterface().SetCollisionGroup(controller->GetInnerBodyID(), group);
                controller->SetLinearVelocity(velocity);
                const auto [inserted, success] = characters.emplace(definition.entity,
                    Character{definition.entity, definition.uuid, settings, transform, controller,
                        standing, crouching, {}, crouched});
                character_lookup[controller->GetInnerBodyID().GetIndex()] = &inserted->second;
                controller->RefreshContacts(world.GetDefaultBroadPhaseLayerFilter(MOVING_LAYER),
                    world.GetDefaultLayerFilter(MOVING_LAYER), filter, {}, allocator);
                return Result<ObjectChange, Error>::success(
                    added ? ObjectChange::Added : ObjectChange::Rebuilt);
            }
            auto& character = it->second;
            character.settings = settings;
            character.controller->SetMaxSlopeAngle(Math::radians(settings.max_slope_angle));
            character.controller->SetMaxStrength(settings.push_force);
            if(!same_pose(character.last_transform, transform)) {
                character.controller->SetPosition(to_position(transform.translation));
                character.controller->SetRotation(to_rotation(transform.rotation));
                character.controller->RefreshContacts(
                    world.GetDefaultBroadPhaseLayerFilter(MOVING_LAYER),
                    world.GetDefaultLayerFilter(MOVING_LAYER), filter, {}, allocator);
            }
            character.last_transform = transform;
            return Result<ObjectChange, Error>::success(ObjectChange::None);
        }

        void move_characters(const float delta_time, std::span<const CharacterRequest> requests) {
            for(auto& [entity, character] : characters) {
                auto& controller = *character.controller;
                const auto& settings = character.settings;
                const LayerBodyFilter filter(JPH::CollisionGroup(
                    layer_filter, settings.collision_layer, settings.collision_mask));
                const auto request = std::ranges::find(requests, entity, &CharacterRequest::entity);
                CharacterMove move;
                const CharacterVelocity* velocity_override = nullptr;
                if(request != requests.end() && request->uuid == character.uuid)
                    move = request->move;
                if(request != requests.end() && request->uuid == character.uuid
                    && request->velocity)
                    velocity_override = &*request->velocity;
                if(move.crouch != character.crouched) {
                    const auto& shape =
                        move.crouch ? character.crouching_shape : character.standing_shape;
                    if(controller.SetShape(shape,
                           1.5f * world.GetPhysicsSettings().mPenetrationSlop,
                           world.GetDefaultBroadPhaseLayerFilter(MOVING_LAYER),
                           world.GetDefaultLayerFilter(MOVING_LAYER), filter, {}, allocator)) {
                        controller.SetInnerBodyShape(shape);
                        character.crouched = move.crouch;
                    }
                }
                character.running = move.run && !character.crouched;
                controller.UpdateGroundVelocity();
                const auto ground = controller.GetGroundVelocity();
                const bool grounded = character_grounded(controller);
                const auto previous = controller.GetLinearVelocity();
                auto velocity = JPH::Vec3(0, previous.GetY(), 0);
                if(grounded) {
                    velocity = ground;
                    if(move.jump)
                        velocity += JPH::Vec3(0, settings.jump_speed, 0);
                }
                float speed = settings.move_speed;
                if(character.crouched)
                    speed = settings.crouch_speed;
                else if(character.running)
                    speed = settings.run_speed;
                auto desired = JPH::Vec3(move.direction.x, 0, move.direction.y) * speed;
                if(velocity_override && velocity_override->horizontal)
                    desired =
                        JPH::Vec3(velocity_override->velocity.x, 0, velocity_override->velocity.z);
                auto horizontal = desired;
                if(!grounded && !(velocity_override && velocity_override->horizontal)) {
                    const auto previous_horizontal = JPH::Vec3(previous.GetX(), 0, previous.GetZ());
                    const auto blend = 1 - std::exp(-settings.air_control * 10 * delta_time);
                    horizontal = previous_horizontal + (desired - previous_horizontal) * blend;
                }
                const auto gravity = world.GetGravity() * settings.gravity_scale;
                velocity += horizontal + gravity * delta_time;
                if(velocity_override && velocity_override->vertical)
                    velocity.SetY(velocity_override->velocity.y + (grounded ? ground.GetY() : 0));
                controller.SetLinearVelocity(velocity);
                const auto before = controller.GetPosition();
                JPH::CharacterVirtual::ExtendedUpdateSettings update;
                update.mWalkStairsStepUp =
                    JPH::Vec3(0, settings.step_height * character.last_transform.scale.x, 0);
                update.mStickToFloorStepDown = JPH::Vec3(
                    0, -settings.ground_snap_distance * character.last_transform.scale.x, 0);
                if(velocity_override && velocity_override->vertical) {
                    update.mWalkStairsStepUp = JPH::Vec3::sZero();
                    update.mStickToFloorStepDown = JPH::Vec3::sZero();
                }
                controller.ExtendedUpdate(delta_time, gravity, update,
                    world.GetDefaultBroadPhaseLayerFilter(MOVING_LAYER),
                    world.GetDefaultLayerFilter(MOVING_LAYER), filter, {}, allocator);
                auto transform = character.last_transform;
                const auto position = controller.GetPosition();
                const auto actual = JPH::Vec3(position - before) / delta_time;
                auto relative = actual;
                if(character_grounded(controller))
                    relative -= controller.GetGroundVelocity();
                character.relative_velocity = {relative.GetX(), relative.GetY(), relative.GetZ()};
                transform.translation = {position.GetX(), position.GetY(), position.GetZ()};
                if(!same_pose(character.last_transform, transform))
                    poses.push_back({character.entity, transform});
                character.last_transform = transform;
            }
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

        Result<ObjectChange, Error> synchronize_body(const BodyDefinition& definition) {
            const auto& transform = definition.transform;
            const auto& collider = definition.collider;
            const auto& rigid = definition.rigid;
            auto it = bodies.find(definition.entity);
            const bool added = it == bodies.end();
            const bool rebuild =
                !added
                && (it->second.uuid != definition.uuid || it->second.rigid.motion != rigid.motion
                    || !same_shape(it->second.collider, collider)
                    || (collider.uses_mesh()
                        && it->second.mesh_revision != mesh_revision(collider.mesh))
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
                    return Result<ObjectChange, Error>::failure(valid.error());
            }
            if(added || rebuild) {
                Velocity velocity;
                if(rebuild && it->second.uuid == definition.uuid
                    && it->second.rigid.motion == BodyMotion::Dynamic
                    && rigid.motion == BodyMotion::Dynamic) {
                    JPH::Vec3 linear, angular;
                    world.GetBodyInterfaceNoLock().GetLinearAndAngularVelocity(
                        it->second.id, linear, angular);
                    velocity = {{linear.GetX(), linear.GetY(), linear.GetZ()},
                        {angular.GetX(), angular.GetY(), angular.GetZ()}};
                }
                if(rebuild)
                    remove_body(it);
                if(auto created = add_body(definition, velocity); !created)
                    return Result<ObjectChange, Error>::failure(created.error());
                return Result<ObjectChange, Error>::success(
                    added ? ObjectChange::Added : ObjectChange::Rebuilt);
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
                    return Result<ObjectChange, Error>::failure(updated.error());
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
            return Result<ObjectChange, Error>::success(ObjectChange::None);
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
                auto first = find_contact_body(pair.first);
                auto second = find_contact_body(pair.second);
                if(!first || !second)
                    continue;
                if(first->uuid > second->uuid)
                    std::swap(first, second);
                const auto [found, added] = tracked_contacts.try_emplace(
                    pair, Contact{first->entity, second->entity, first->uuid, second->uuid,
                              first->trigger || second->trigger});
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
                const auto first = find_contact_body(pair.first);
                const auto second = find_contact_body(pair.second);
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
        const MeshSource& mesh_source;
        JoltLifetime lifetime;
        std::unordered_map<AssetHandle, CachedMesh> mesh_shapes;
        JPH::Ref<LayerGroupFilter> layer_filter = new LayerGroupFilter;
        JPH::ObjectLayerPairFilterTable pairs;
        JPH::BroadPhaseLayerInterfaceTable broad_phase;
        std::unique_ptr<JPH::ObjectVsBroadPhaseLayerFilterTable> object_filter;
        ContactCollector collector;
        JPH::PhysicsSystem world;
        JPH::TempAllocatorImplWithMallocFallback allocator{SCRATCH_BYTES};
        JPH::JobSystemSingleThreaded jobs;
        std::vector<Body*> mesh_bodies;
        std::unordered_map<EntityId, Body> bodies;
        std::vector<Body*> body_lookup;
        std::map<EntityId, Character> characters;
        std::vector<Character*> character_lookup;
        std::vector<JPH::BodyID> step_active_bodies;
        std::vector<Pair> frame_contacts;
        std::map<Pair, Contact> tracked_contacts;
        // unordered_map 的重哈希不移动 Body；移除时先从该列表摘除。
        std::vector<Body*> kinematic_bodies;
        JointMap joints;
        // map 插入不移动节点；移除关节前先从可断裂列表摘除。
        std::vector<JointMap::iterator> breakable_joints;
        std::vector<JointBreak> broken_joints;
    };

    Result<void, Error> PhysicsService::step(const float delta_time) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        const ScopeExit clear([this] {
            m_commands.clear();
            m_character_moves.clear();
        });
        m_impl->release_unused_shapes();
        m_impl->move_kinematic_bodies(delta_time);
        if(auto applied = m_impl->apply_commands(m_commands, delta_time); !applied)
            return applied;
        m_impl->poses.clear();
        m_impl->move_characters(delta_time, m_character_moves);
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
        m_impl->collect_broken_joints(delta_time);
        m_impl->publish_contacts();
        return Result<void, Error>::success();
    }

    Result<std::vector<PhysicsService::ColliderEdge>, Error> PhysicsService::mesh_collider_edges(
        ColliderShape kind, const MeshData& mesh, Math::Vec3 scale) {
        using Edges = Result<std::vector<ColliderEdge>, Error>;
        const JoltLifetime lifetime;
        auto built = create_mesh_shape(kind, mesh, scale);
        if(!built)
            return Edges::failure(built.error());
        const auto& shape = built.value();
        JPH::Shape::GetTrianglesContext context;
        shape->GetTrianglesStart(context, JPH::AABox::sBiggest(), shape->GetCenterOfMass(),
            JPH::Quat::sIdentity(), JPH::Vec3::sOne());
        // 一个凸包面最多 256 点，单批须能容纳整面，避免误将空批当作结束。
        constexpr int BATCH_TRIANGLES = 256;
        std::array<JPH::Float3, BATCH_TRIANGLES * 3> vertices;
        std::vector<ColliderEdge> edges;
        const auto key = [](Math::Vec3 point) { return std::tuple{point.x, point.y, point.z}; };
        while(
            const auto count = shape->GetTrianglesNext(context, BATCH_TRIANGLES, vertices.data())) {
            for(int triangle = 0; triangle < count; ++triangle) {
                const auto* points = vertices.data() + triangle * 3;
                for(int edge = 0; edge < 3; ++edge) {
                    const auto& a = points[edge];
                    const auto& b = points[(edge + 1) % 3];
                    Math::Vec3 first{a.x, a.y, a.z}, second{b.x, b.y, b.z};
                    if(!Math::is_finite(first) || !Math::is_finite(second))
                        return Edges::failure({"Collision outline contains nonfinite vertices"});
                    if(key(second) < key(first))
                        std::swap(first, second);
                    if(first != second)
                        edges.emplace_back(first, second);
                }
            }
        }
        std::ranges::sort(edges, {},
            [&](const ColliderEdge& edge) { return std::pair{key(edge.first), key(edge.second)}; });
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        return Edges::success(std::move(edges));
    }

    Result<std::vector<Math::Vec3>, Error> PhysicsService::static_triangles(
        Math::Vec3 minimum, Math::Vec3 maximum) const {
        using Output = Result<std::vector<Math::Vec3>, Error>;
        if(!m_impl || !has_binding() || !Math::is_finite(minimum) || !Math::is_finite(maximum)
            || !glm::all(glm::lessThan(minimum, maximum)))
            return Output::failure({"Static geometry requires an active world and valid bounds"});
        std::vector<Math::Vec3> output;
        const JPH::AABox bounds(
            JPH::Vec3(minimum.x, minimum.y, minimum.z), JPH::Vec3(maximum.x, maximum.y, maximum.z));
        // 稳定顺序使同一内容的导航构建可复现。
        std::vector<const Impl::Body*> bodies;
        for(const auto& [id, body] : m_impl->bodies)
            if(body.rigid.motion == BodyMotion::Static && !body.collider.is_trigger)
                bodies.push_back(&body);
        std::ranges::sort(bodies, {}, [](const auto* body) { return body->uuid; });
        for(const auto* body : bodies) {
            const auto shape = m_impl->world.GetBodyInterface().GetTransformedShape(body->id);
            JPH::TransformedShape::GetTrianglesContext context;
            shape.GetTrianglesStart(context, bounds, JPH::RVec3::sZero());
            std::array<JPH::Float3, 768> vertices;
            while(const int count = shape.GetTrianglesNext(context, 256, vertices.data())) {
                if(output.size() + count * 3 > 3000000)
                    return Output::failure({"Navigation geometry exceeds one million triangles"});
                for(int i = 0; i < count * 3; ++i)
                    output.emplace_back(vertices[i].x, vertices[i].y, vertices[i].z);
            }
        }
        return Output::success(std::move(output));
    }

    PhysicsService::PhysicsService(MeshSource meshes) : m_meshes(std::move(meshes)) {}
    PhysicsService::Statistics PhysicsService::get_statistics() const {
        if(!m_impl)
            return {};
        return {.bodies = m_impl->bodies.size(),
            .active_bodies = m_impl->world.GetNumActiveBodies(JPH::EBodyType::RigidBody),
            .pose_updates = m_impl->poses.size(),
            .joints = m_impl->joints.size(),
            .characters = m_impl->characters.size()};
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
        m_character_moves.clear();
        m_impl.reset();
    }

    Result<void, Error> PhysicsService::prepare_world() {
        if(!has_binding())
            return Result<void, Error>::failure({"Physics service is inactive"});
        if(m_impl)
            return Result<void, Error>::failure({"Physics world is already active"});
        m_impl = std::make_unique<Impl>(m_meshes);
        return Result<void, Error>::success();
    }

    Result<PhysicsService::ObjectChange, Error> PhysicsService::synchronize_body(
        const BodyDefinition& definition) {
        if(!m_impl)
            return Result<ObjectChange, Error>::failure({"Physics world is not active"});
        return m_impl->synchronize_body(definition);
    }

    Result<bool, Error> PhysicsService::synchronize_meshes() {
        return m_impl->synchronize_meshes();
    }

    Result<PhysicsService::ObjectChange, Error> PhysicsService::synchronize_character(
        const CharacterDefinition& definition) {
        if(!m_impl)
            return Result<ObjectChange, Error>::failure({"Physics world is not active"});
        return m_impl->synchronize_character(definition);
    }

    void PhysicsService::remove_characters(
        const std::span<const std::pair<EntityUuid, EntityId>> definitions) {
        if(m_impl)
            m_impl->remove_characters(definitions);
    }

    bool PhysicsService::enqueue_character_move(
        const EntityUuid uuid, const EntityId entity, const CharacterMove move) {
        const auto found = std::ranges::find(m_character_moves, entity, &CharacterRequest::entity);
        if(found != m_character_moves.end()) {
            if(found->uuid != uuid)
                found->velocity.reset();
            found->uuid = uuid;
            found->move = move;
            return true;
        }
        if(m_character_moves.size() >= MAX_COMMANDS)
            return false;
        m_character_moves.push_back({uuid, entity, move});
        return true;
    }

    bool PhysicsService::enqueue_character_velocity(
        const EntityUuid uuid, const EntityId entity, const CharacterVelocity velocity) {
        const auto found = std::ranges::find(m_character_moves, entity, &CharacterRequest::entity);
        if(found != m_character_moves.end()) {
            if(found->uuid != uuid)
                found->move = {};
            found->uuid = uuid;
            found->velocity = velocity;
            return true;
        }
        if(m_character_moves.size() >= MAX_COMMANDS)
            return false;
        m_character_moves.push_back({uuid, entity, {}, velocity});
        return true;
    }

    std::optional<PhysicsAccess::CharacterState> PhysicsService::read_character_state(
        const EntityUuid uuid, const EntityId entity) const {
        if(!m_impl)
            return std::nullopt;
        const auto found = m_impl->characters.find(entity);
        if(found == m_impl->characters.end() || found->second.uuid != uuid)
            return std::nullopt;
        const auto& controller = *found->second.controller;
        const auto velocity = controller.GetLinearVelocity();
        const auto normal = controller.GetGroundNormal();
        const auto& character = found->second;
        const float height = controller.GetShape()->GetLocalBounds().GetSize().GetY();
        Math::Vec3 desired{0};
        const auto pending =
            std::ranges::find(m_character_moves, entity, &CharacterRequest::entity);
        if(pending != m_character_moves.end() && pending->uuid == uuid) {
            const auto& move = pending->move;
            float speed = character.settings.move_speed;
            if(move.crouch)
                speed = character.settings.crouch_speed;
            else if(move.run)
                speed = character.settings.run_speed;
            desired = Math::Vec3(move.direction.x, 0, move.direction.y) * speed;
        }
        return CharacterState{{velocity.GetX(), velocity.GetY(), velocity.GetZ()},
            {normal.GetX(), normal.GetY(), normal.GetZ()}, character_grounded(controller),
            character.relative_velocity, character.running, character.crouched,
            height / (character.settings.height * character.last_transform.scale.x), desired};
    }

    Result<std::optional<PhysicsAccess::QueryHit>> PhysicsService::query_shape(
        const ColliderComponent& collider, const TransformComponent& transform,
        Math::Vec3 displacement, const QueryFilter& options) const {
        using Hits = Result<std::optional<QueryHit>>;
        if(!m_impl)
            return Hits::failure("Physics world has not been synchronized");
        if(auto valid = validate_collider_geometry(transform, collider); !valid)
            return Hits::failure(valid.error().message);
        if(collider.shape == ColliderShape::TriangleMesh)
            return Hits::failure("Shape cast requires a convex shape, not a triangle mesh");
        auto created = m_impl->collider_shape(collider, transform.scale);
        if(!created)
            return Hits::failure(created.error().message);
        const auto& shape = created.value();
        const auto origin = to_position(transform.translation);
        const auto cast = JPH::RShapeCast::sFromWorldTransform(shape, JPH::Vec3::sOne(),
            JPH::RMat44::sRotationTranslation(to_rotation(transform.rotation), origin),
            JPH::Vec3(displacement.x, displacement.y, displacement.z));
        JPH::ShapeCastSettings settings;
        settings.mBackFaceModeConvex = JPH::EBackFaceMode::CollideWithBackFaces;
        settings.mReturnDeepestPoint = true;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const Impl::QueryBodyFilter filter(*m_impl, options);
        m_impl->world.GetNarrowPhaseQuery().CastShape(
            cast, settings, origin, collector, {}, {}, filter);
        if(!collector.HadHit())
            return Hits::success(std::nullopt);
        const auto& hit = collector.mHit;
        const auto owner = m_impl->find_contact_body(hit.mBodyID2);
        if(!owner)
            return Hits::failure("Shape cast body is no longer available");
        const auto point = origin + hit.mContactPointOn2;
        const auto normal = -hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero());
        return Hits::success(QueryHit{owner->uuid, {point.GetX(), point.GetY(), point.GetZ()},
            {normal.GetX(), normal.GetY(), normal.GetZ()}, std::clamp(hit.mFraction, 0.0f, 1.0f)});
    }

    Result<std::optional<PhysicsAccess::QueryHit>> PhysicsService::query_ray(
        Math::Vec3 origin, Math::Vec3 displacement, const QueryFilter& options) const {
        using Hits = Result<std::optional<QueryHit>>;
        if(!m_impl)
            return Hits::failure("Physics world has not been synchronized");
        const JPH::RRayCast ray(
            to_position(origin), JPH::Vec3(displacement.x, displacement.y, displacement.z));
        JPH::RayCastResult hit;
        const Impl::QueryBodyFilter filter(*m_impl, options);
        if(!m_impl->world.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter))
            return Hits::success(std::nullopt);
        const auto owner = m_impl->find_contact_body(hit.mBodyID);
        const JPH::BodyLockRead lock(m_impl->world.GetBodyLockInterface(), hit.mBodyID);
        if(!owner || !lock.Succeeded())
            return Hits::failure("Raycast body is no longer available");
        const auto point = origin + hit.mFraction * displacement;
        Math::Vec3 normal = -glm::normalize(displacement);
        if(hit.mFraction > 0) {
            const auto surface =
                lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, to_position(point));
            normal = {surface.GetX(), surface.GetY(), surface.GetZ()};
        }
        return Hits::success(QueryHit{owner->uuid, point, normal, hit.mFraction});
    }

    Result<std::vector<EntityUuid>> PhysicsService::query_overlap(
        Math::Vec3 center, float radius, const QueryFilter& options) const {
        using Hits = Result<std::vector<EntityUuid>>;
        if(!m_impl)
            return Hits::failure("Physics world has not been synchronized");
        const JPH::SphereShape sphere(radius);
        const JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const Impl::QueryBodyFilter filter(*m_impl, options);
        m_impl->world.GetNarrowPhaseQuery().CollideShape(&sphere, JPH::Vec3::sOne(),
            JPH::RMat44::sTranslation(to_position(center)), settings, to_position(center),
            collector, {}, {}, filter);
        std::vector<EntityUuid> entities;
        entities.reserve(collector.mHits.size());
        for(const auto& hit : collector.mHits)
            if(const auto owner = m_impl->find_contact_body(hit.mBodyID2))
                entities.push_back(owner->uuid);
        std::ranges::sort(entities);
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
        return Hits::success(std::move(entities));
    }

    Result<void, Error> PhysicsService::synchronize_joints(
        const std::span<const JointDefinition> definitions) {
        if(!m_impl)
            return Result<void, Error>::failure({"Physics world is not active"});
        return m_impl->synchronize_joints(definitions);
    }

    void PhysicsService::remove_bodies(
        const std::span<const std::pair<EntityUuid, EntityId>> definitions) {
        if(!m_impl)
            return;
        m_impl->remove_bodies(definitions);
    }

    std::span<const PhysicsService::Pose> PhysicsService::poses() const {
        return m_impl->poses;
    }
    std::span<const PhysicsService::ContactChange> PhysicsService::contacts() const {
        return m_impl->changes;
    }
    std::span<const PhysicsService::JointBreak> PhysicsService::broken_joints() const {
        return m_impl->broken_joints;
    }
}
