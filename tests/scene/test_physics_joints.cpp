#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/systems/physics_system.h"

#include <gtest/gtest.h>
#include <limits>
#include <utility>

namespace Comet::Tests {
    class PhysicsJointTest: public ::testing::Test {
    protected:
        Scene scene;
        PhysicsService physics;
        SceneRuntime runtime;

        void SetUp() override {
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
        }

        Entity body(const char* name, BodyMotion motion, Math::Vec3 position) {
            auto entity = scene.create_entity(name);
            entity.edit_transform([&](auto& transform) { transform.translation = position; });
            entity.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = motion});
            entity.add_component<ColliderComponent>();
            return entity;
        }

        Math::Vec3 anchor_position(Entity entity, Math::Vec3 anchor) {
            return Math::Vec3(scene.get_world_matrix(entity) * Math::Vec4(anchor, 1));
        }
    };

    class PointJointTest: public PhysicsJointTest {};
    class HingeJointTest: public PhysicsJointTest {};

    TEST_F(PointJointTest, WorldAnchorConstrainsPositionAndAllowsRotation) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_anchor = {0, 5, 0}});
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        ASSERT_TRUE(physics.request_impulse(actor, {3, 4, 5}));
        ASSERT_TRUE(physics.request_torque(actor, {0, 0, 5}));
        for(int i = 0; i < 30; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(glm::length(
                      actor.get_component<TransformComponent>().translation - Math::Vec3(0, 5, 0)),
            0.001f);
        const auto velocity = physics.get_velocity(actor);
        ASSERT_TRUE(velocity);
        EXPECT_LT(glm::length(velocity->linear), 0.001f);
        EXPECT_GT(velocity->angular.z, 0.1f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
    }

    TEST_F(PointJointTest, PendulumUsesRotatedAndScaledLocalAnchor) {
        auto actor = body("Pendulum", BodyMotion::Dynamic, {1, 4, 0});
        actor.edit_transform([](auto& transform) {
            transform.rotation.z = 35;
            transform.scale = {0.2f, 0.6f, 0.2f};
        });
        const Math::Vec3 local{0, 0.5f, 0};
        const auto pivot = anchor_position(actor, local);
        actor.add_component<PointJointComponent>(
            PointJointComponent{.anchor = local, .connected_anchor = pivot});
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 150; ++i) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_LT(glm::length(anchor_position(actor, local) - pivot), 0.005f);
        }
        EXPECT_NE(actor.get_component<TransformComponent>().rotation.z, 35);
        EXPECT_EQ(physics.get_statistics().joints, 1u);
    }

    TEST_F(PointJointTest, ConnectsDynamicBodyFromEitherEndAndToMovingKinematicBody) {
        for(const auto& [first_motion, second_motion] :
            {std::pair{BodyMotion::Dynamic, BodyMotion::Dynamic},
                std::pair{BodyMotion::Dynamic, BodyMotion::Kinematic},
                std::pair{BodyMotion::Static, BodyMotion::Dynamic}}) {
            SCOPED_TRACE(static_cast<int>(first_motion));
            SCOPED_TRACE(static_cast<int>(second_motion));
            auto target = body("Target", second_motion, {0, 5, 0});
            target.edit_transform([](auto& transform) { transform.scale.y = 2; });
            auto actor = body("Actor", first_motion, {0, 2, 0});
            actor.add_component<PointJointComponent>(
                PointJointComponent{.connected_body = target.get_uuid(),
                    .anchor = {0, 1, 0},
                    .connected_anchor = {0, -1, 0}});
            ASSERT_TRUE(runtime.start(scene));
            if(second_motion == BodyMotion::Kinematic)
                target.edit_transform([](auto& transform) { transform.translation.x = 0.2f; });
            else
                ASSERT_TRUE(physics.request_impulse(
                    first_motion == BodyMotion::Dynamic ? actor : target, {1, 0, 0}));
            for(int i = 0; i < 30; ++i) {
                ASSERT_TRUE(runtime.advance(0.01));
                EXPECT_LT(glm::length(anchor_position(actor, {0, 1, 0})
                                      - anchor_position(target, {0, -1, 0})),
                    0.03f);
            }
            const auto moved = first_motion == BodyMotion::Dynamic ? actor : target;
            EXPECT_GT(moved.get_component<TransformComponent>().translation.x, 0);
            EXPECT_EQ(physics.get_statistics().joints, 1u);
            ASSERT_TRUE(runtime.stop());
            scene.destroy_entity(actor);
            scene.destroy_entity(target);
        }
    }

    TEST_F(PointJointTest, PauseAndEnableChangesPreserveAngularVelocity) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_anchor = {0, 5, 0}});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_torque(actor, {0, 0, 5}));
        ASSERT_TRUE(runtime.advance(0.01));
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        const auto angular = physics.get_velocity(actor)->angular.z;
        actor.edit_component<PointJointComponent>([](auto& joint) { joint.enabled = false; });
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        EXPECT_FLOAT_EQ(physics.get_velocity(actor)->angular.z, angular);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        EXPECT_LT(actor.get_component<TransformComponent>().translation.y, 5);
        EXPECT_NEAR(physics.get_velocity(actor)->angular.z, angular * 0.9995f, 1e-5f);
        actor.edit_component<PointJointComponent>([](auto& joint) {
            joint.enabled = true;
            joint.connected_anchor.x = 0.2f;
        });
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        EXPECT_NEAR(physics.get_velocity(actor)->angular.z, angular * 0.9995f * 0.9995f, 1e-5f);
    }

    TEST_F(PointJointTest, RebindsAfterEitherBodyRebuildAndRemovesBeforeBodyDestruction) {
        auto target = body("Target", BodyMotion::Static, {0, 5, 0});
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 3, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = target.get_uuid(), .anchor = {0, 2, 0}});
        ASSERT_TRUE(runtime.start(scene));
        for(const auto changed : {actor, target}) {
            changed.edit_component<ColliderComponent>(
                [](auto& collider) { collider.half_extents.x = 0.7f; });
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(physics.get_statistics().joints, 1u);
            EXPECT_LT(glm::length(anchor_position(actor, {0, 2, 0}) - anchor_position(target, {})),
                0.005f);
        }
        target.remove_component<RigidBodyComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        scene.destroy_entity(target);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        // 新实体不能接管旧 UUID 的连接，失效引用也不能隐式固定到世界。
        body("Replacement", BodyMotion::Static, {0, 5, 0});
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        EXPECT_TRUE(actor.get_component<PointJointComponent>().connected_body);
        actor.edit_component<PointJointComponent>([](auto& joint) {
            joint.connected_body = {};
            joint.connected_anchor = {0, 5, 0};
        });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        scene.destroy_entity(actor);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
    }

    TEST_F(PointJointTest, ComponentRemovalAndMissingReferencesDoNotCreateWorldConstraints) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = EntityUuid::generate()});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        EXPECT_LT(actor.get_component<TransformComponent>().translation.y, 5);
        actor.edit_component<PointJointComponent>([](auto& joint) {
            joint.connected_body = {};
            joint.connected_anchor = {0, 5, 0};
        });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        actor.remove_component<PointJointComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
    }

    TEST_F(PointJointTest, InvalidConfigurationFailsStartAndFixedSyncWithCleanRuntime) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_anchor = {0, 5, 0}});
        const auto valid = actor.get_component<PointJointComponent>();
        auto self = valid;
        self.connected_body = actor.get_uuid();
        auto invalid = valid;
        invalid.anchor.x = std::numeric_limits<float>::quiet_NaN();
        auto overflow = valid;
        overflow.connected_anchor.x = std::numeric_limits<float>::max();
        for(const auto& settings : {self, invalid, overflow}) {
            actor.edit_component<PointJointComponent>([&](auto& joint) { joint = settings; });
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(physics.get_statistics().bodies, 0u);
            EXPECT_EQ(physics.get_statistics().joints, 0u);
            actor.edit_component<PointJointComponent>([&](auto& joint) { joint = valid; });
            ASSERT_TRUE(runtime.start(scene));
            actor.edit_component<PointJointComponent>([&](auto& joint) { joint = settings; });
            EXPECT_FALSE(runtime.advance(0.01));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(physics.get_statistics().joints, 0u);
        }
        actor.edit_component<PointJointComponent>([&](auto& joint) { joint = valid; });
        actor.edit_component<RigidBodyComponent>(
            [](auto& rigid) { rigid.motion = BodyMotion::Static; });
        EXPECT_FALSE(runtime.start(scene));
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(physics.get_statistics().joints, 0u);
    }
    TEST_F(HingeJointTest, WorldConnectionLocksPositionAndOnlyAllowsRotationAroundItsAxis) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_anchor = {0, 5, 0}});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(physics.request_impulse(actor, {3, 4, 5}));
        ASSERT_TRUE(physics.request_torque(actor, {4, 5, 6}));
        for(int i = 0; i < 30; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_LT(glm::length(
                      actor.get_component<TransformComponent>().translation - Math::Vec3(0, 5, 0)),
            0.001f);
        const auto velocity = physics.get_velocity(actor);
        ASSERT_TRUE(velocity);
        EXPECT_LT(glm::length(velocity->linear), 0.001f);
        EXPECT_NEAR(velocity->angular.x, 0, 1e-5f);
        EXPECT_NEAR(velocity->angular.y, 0, 1e-5f);
        EXPECT_GT(velocity->angular.z, 0.1f);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
    }

    TEST_F(HingeJointTest, AxesUseRotationWithoutScaleAndNormalizeFiniteExtremeDirections) {
        const auto large = std::numeric_limits<float>::max();
        const auto small = std::numeric_limits<float>::min();
        for(const auto magnitude : {1.0f, large, small}) {
            SCOPED_TRACE(magnitude);
            auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
            actor.edit_transform([](auto& transform) {
                transform.rotation.z = 90;
                transform.scale = {2, 3, 4};
            });
            actor.add_component<HingeJointComponent>(
                HingeJointComponent{.connected_anchor = {0, 5, 0},
                    .axis = {magnitude, magnitude, 0},
                    .connected_axis = {-magnitude, magnitude, 0}});
            ASSERT_TRUE(runtime.start(scene));
            const auto direction = Math::normalize(Math::Vec3(-1, 1, 0));
            ASSERT_TRUE(physics.request_torque(actor, direction * 5.0f));
            for(int i = 0; i < 20; ++i)
                ASSERT_TRUE(runtime.advance(0.01));
            const auto velocity = physics.get_velocity(actor);
            ASSERT_TRUE(velocity);
            EXPECT_GT(Math::dot(velocity->angular, direction), 0.001f);
            EXPECT_LT(Math::length(Math::cross(velocity->angular, direction)), 1e-5f);
            EXPECT_TRUE(Math::is_finite(actor.get_component<TransformComponent>().rotation));
            ASSERT_TRUE(runtime.stop());
            scene.destroy_entity(actor);
        }
    }

    TEST_F(HingeJointTest, BothBodyAxesFollowLocalRotationAndKinematicMotion) {
        for(const auto& [first_motion, second_motion] :
            {std::pair{BodyMotion::Dynamic, BodyMotion::Dynamic},
                std::pair{BodyMotion::Dynamic, BodyMotion::Kinematic},
                std::pair{BodyMotion::Static, BodyMotion::Dynamic}}) {
            auto actor = body("Actor", first_motion, {0, 3, 0});
            auto target = body("Target", second_motion, {0, 5, 0});
            target.edit_transform([](auto& transform) {
                transform.rotation.z = 90;
                transform.scale = {2, 3, 4};
            });
            actor.add_component<HingeJointComponent>(
                HingeJointComponent{.connected_body = target.get_uuid(),
                    .anchor = {0, 2, 0},
                    .axis = {0, 1, 0},
                    .connected_axis = {1, 0, 0}});
            ASSERT_TRUE(runtime.start(scene));
            const auto moving = first_motion == BodyMotion::Dynamic ? actor : target;
            ASSERT_TRUE(physics.request_torque(moving, {1, 5, 3}));
            if(second_motion == BodyMotion::Kinematic)
                target.edit_transform([](auto& transform) { transform.translation.x += 0.2f; });
            for(int i = 0; i < 50; ++i)
                ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_LT(Math::length(anchor_position(actor, {0, 2, 0}) - anchor_position(target, {})),
                0.02f);
            static_cast<void>(scene.get_world_matrix(actor));
            static_cast<void>(scene.get_world_matrix(target));
            const auto first_axis =
                Math::Vec3(actor.get_component<WorldTransformComponent>().pose_world_matrix
                           * Math::Vec4(0, 1, 0, 0));
            const auto second_axis =
                Math::Vec3(target.get_component<WorldTransformComponent>().pose_world_matrix
                           * Math::Vec4(1, 0, 0, 0));
            EXPECT_GT(Math::dot(first_axis, second_axis), 0.999f);
            EXPECT_EQ(physics.get_statistics().joints, 1u);
            ASSERT_TRUE(runtime.stop());
            scene.destroy_entity(actor);
            scene.destroy_entity(target);
        }
    }

    TEST_F(HingeJointTest, JointKindsHaveIndependentIdentityAcrossEditsRebuildAndRemoval) {
        auto target = body("Target", BodyMotion::Static, {0, 5, 0});
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 3, 0});
        actor.add_component<PointJointComponent>(
            PointJointComponent{.connected_body = target.get_uuid(), .anchor = {0, 2, 0}});
        actor.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_body = target.get_uuid(), .anchor = {0, 2, 0}});
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(physics.get_statistics().joints, 2u);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        actor.edit_component<HingeJointComponent>([](auto& joint) { joint.enabled = false; });
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_EQ(physics.get_statistics().joints, 2u);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        actor.edit_component<HingeJointComponent>([](auto& joint) {
            joint.enabled = true;
            joint.axis = joint.connected_axis = {0, 1, 0};
        });
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(physics.get_statistics().joints, 2u);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Running));
        for(const auto changed : {actor, target}) {
            changed.edit_component<ColliderComponent>(
                [](auto& collider) { collider.half_extents.x = 0.7f; });
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_EQ(physics.get_statistics().joints, 2u);
            EXPECT_LT(Math::length(anchor_position(actor, {0, 2, 0}) - anchor_position(target, {})),
                0.005f);
        }
        actor.remove_component<PointJointComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        ASSERT_TRUE(physics.request_torque(actor, {5, 5, 5}));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(physics.get_velocity(actor)->angular.x, 0, 1e-5f);
        EXPECT_GT(physics.get_velocity(actor)->angular.y, 0.001f);
        EXPECT_NEAR(physics.get_velocity(actor)->angular.z, 0, 1e-5f);
        actor.remove_component<HingeJointComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        actor.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_body = target.get_uuid(), .anchor = {0, 2, 0}});
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        target.remove_component<RigidBodyComponent>();
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        target.add_component<RigidBodyComponent>(RigidBodyComponent{.motion = BodyMotion::Static});
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        const auto reference = target.get_uuid();
        scene.destroy_entity(target);
        body("Replacement", BodyMotion::Static, {0, 5, 0});
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
        EXPECT_EQ(actor.get_component<HingeJointComponent>().connected_body, reference);
        actor.edit_component<HingeJointComponent>([](auto& joint) {
            joint.connected_body = {};
            joint.connected_anchor = {0, 5, 0};
        });
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 1u);
        scene.destroy_entity(actor);
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_EQ(physics.get_statistics().joints, 0u);
    }

    TEST_F(HingeJointTest, ZeroAndNonFiniteAxesFailStartAndLiveSyncWithoutLeakingNativeObjects) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<HingeJointComponent>(
            HingeJointComponent{.connected_anchor = {0, 5, 0}});
        const auto valid = actor.get_component<HingeJointComponent>();
        for(const auto member :
            {&HingeJointComponent::axis, &HingeJointComponent::connected_axis}) {
            for(const auto direction :
                {Math::Vec3(0), Math::Vec3(std::numeric_limits<float>::quiet_NaN(), 0, 1),
                    Math::Vec3(0, std::numeric_limits<float>::infinity(), 1)}) {
                auto invalid = valid;
                invalid.*member = direction;
                ASSERT_TRUE(actor.try_set_component(invalid));
                const auto start = runtime.start(scene);
                EXPECT_FALSE(start);
                if(!start)
                    EXPECT_NE(start.error().message.find("Hinge axes"), std::string::npos);
                EXPECT_FALSE(runtime.is_active());
                EXPECT_EQ(physics.get_statistics().bodies, 0u);
                EXPECT_EQ(physics.get_statistics().joints, 0u);
                ASSERT_TRUE(actor.try_set_component(valid));
                ASSERT_TRUE(runtime.start(scene));
                ASSERT_TRUE(actor.try_set_component(invalid));
                EXPECT_FALSE(runtime.advance(0.01));
                EXPECT_FALSE(runtime.is_active());
                EXPECT_EQ(physics.get_statistics().joints, 0u);
            }
        }
    }

    TEST_F(
        HingeJointTest, MotorReversesAndRespectsAsymmetricLimitsWithPersistentZeroAcrossRebuild) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<HingeJointComponent>(HingeJointComponent{.connected_anchor = {0, 5, 0},
            .limits_enabled = true,
            .min_angle = -20,
            .max_angle = 55,
            .motor_enabled = true,
            .motor_speed = 90,
            .motor_max_torque = 10});
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 150; ++i) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_LE(actor.get_component<TransformComponent>().rotation.z, 56);
        }
        EXPECT_NEAR(actor.get_component<TransformComponent>().rotation.z, 55, 0.2f);
        actor.edit_component<HingeJointComponent>([](auto& joint) {
            joint.max_angle = 40;
            joint.motor_enabled = false;
        });
        actor.edit_component<ColliderComponent>(
            [](auto& collider) { collider.half_extents.x = 0.7f; });
        for(int i = 0; i < 30; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(actor.get_component<TransformComponent>().rotation.z, 40, 0.2f);
        actor.edit_component<HingeJointComponent>([](auto& joint) {
            joint.motor_enabled = true;
            joint.motor_speed = -90;
        });
        for(int i = 0; i < 150; ++i) {
            ASSERT_TRUE(runtime.advance(0.01));
            EXPECT_GE(actor.get_component<TransformComponent>().rotation.z, -21);
        }
        EXPECT_NEAR(actor.get_component<TransformComponent>().rotation.z, -20, 0.2f);
        EXPECT_EQ(physics.get_statistics().joints, 1u);
    }

    TEST_F(HingeJointTest, ReferenceDirectionsDefineZeroAndUseRotationWithoutScale) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.edit_transform([](auto& value) {
            value.rotation.z = 90;
            value.scale = {2, 3, 4};
        });
        actor.add_component<HingeJointComponent>(HingeJointComponent{.connected_anchor = {0, 5, 0},
            .limits_enabled = true,
            .reference_axis = {1, 0, 1},
            .connected_reference_axis = {0, 1, 1},
            .min_angle = -20,
            .max_angle = 40,
            .motor_enabled = true,
            .motor_speed = 90});
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 180; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(actor.get_component<TransformComponent>().rotation.z, 130, 0.2f);
        actor.edit_component<HingeJointComponent>([](auto& value) { value.motor_speed = -90; });
        for(int i = 0; i < 180; ++i)
            ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_NEAR(actor.get_component<TransformComponent>().rotation.z, 70, 0.2f);
    }

    TEST_F(HingeJointTest, MotorTargetsOwnerRelativeToConnectedBodyFromEitherDynamicEnd) {
        for(const auto& [first_motion, second_motion] :
            {std::pair{BodyMotion::Dynamic, BodyMotion::Dynamic},
                std::pair{BodyMotion::Dynamic, BodyMotion::Kinematic},
                std::pair{BodyMotion::Static, BodyMotion::Dynamic}}) {
            auto actor = body("Actor", first_motion, {0, 5, 0});
            auto target = body("Target", second_motion, {0, 5, 0});
            actor.edit_component<ColliderComponent>(
                [](auto& collider) { collider.is_trigger = true; });
            actor.add_component<HingeJointComponent>(
                HingeJointComponent{.connected_body = target.get_uuid(),
                    .motor_enabled = true,
                    .motor_speed = 90,
                    .motor_max_torque = 10});
            ASSERT_TRUE(runtime.start(scene));
            for(int i = 0; i < 50; ++i)
                ASSERT_TRUE(runtime.advance(0.01));
            const auto relative =
                physics.get_velocity(actor)->angular - physics.get_velocity(target)->angular;
            EXPECT_NEAR(relative.z, Math::radians(90), 1e-4f);
            EXPECT_NEAR(relative.x, 0, 1e-5f);
            EXPECT_NEAR(relative.y, 0, 1e-5f);
            ASSERT_TRUE(runtime.stop());
            scene.destroy_entity(actor);
            scene.destroy_entity(target);
        }
    }

    TEST_F(HingeJointTest, MotorTorqueCapsAccelerationAndPauseChangesTakeEffectOnSingleStep) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        actor.add_component<HingeJointComponent>(HingeJointComponent{.connected_anchor = {0, 5, 0},
            .motor_enabled = true,
            .motor_speed = 90,
            .motor_max_torque = 0});
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.advance(0.01));
        EXPECT_FLOAT_EQ(physics.get_velocity(actor)->angular.z, 0);
        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        actor.edit_component<HingeJointComponent>(
            [](auto& joint) { joint.motor_max_torque = 0.1f; });
        ASSERT_TRUE(runtime.advance(0.1));
        EXPECT_FLOAT_EQ(physics.get_velocity(actor)->angular.z, 0);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        const auto angular = physics.get_velocity(actor)->angular.z;
        EXPECT_GT(angular, 0);
        EXPECT_LT(angular, 0.01f);
        actor.edit_component<HingeJointComponent>([](auto& joint) { joint.motor_enabled = false; });
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_NEAR(physics.get_velocity(actor)->angular.z, angular * 0.9995f, 1e-5f);
        EXPECT_EQ(physics.get_statistics().joints, 1u);
    }

    TEST_F(HingeJointTest, InvalidLimitsReferencesAndMotorSettingsFailStartAndLiveSync) {
        auto actor = body("Actor", BodyMotion::Dynamic, {0, 5, 0});
        const HingeJointComponent valid{
            .connected_anchor = {0, 5, 0}, .limits_enabled = true, .motor_enabled = true};
        actor.add_component<HingeJointComponent>(valid);
        std::vector<HingeJointComponent> invalid;
        for(const auto& [minimum, maximum] : {std::pair{1.0f, 30.0f}, std::pair{-30.0f, -1.0f},
                std::pair{-181.0f, 30.0f}, std::pair{-30.0f, 181.0f}, std::pair{0.0f, 0.0f}}) {
            auto value = valid;
            value.min_angle = minimum;
            value.max_angle = maximum;
            invalid.push_back(value);
        }
        for(const auto member : {&HingeJointComponent::reference_axis,
                &HingeJointComponent::connected_reference_axis}) {
            for(const auto direction : {Math::Vec3(0), Math::Vec3(0, 0, 1)}) {
                auto value = valid;
                value.*member = direction;
                invalid.push_back(value);
            }
        }
        for(const auto member : {&HingeJointComponent::min_angle, &HingeJointComponent::max_angle,
                &HingeJointComponent::motor_speed, &HingeJointComponent::motor_max_torque}) {
            auto value = valid;
            value.*member = std::numeric_limits<float>::quiet_NaN();
            invalid.push_back(value);
        }
        auto negative_torque = valid;
        negative_torque.motor_max_torque = -1;
        invalid.push_back(negative_torque);
        for(const auto& value : invalid) {
            ASSERT_TRUE(actor.try_set_component(value));
            EXPECT_FALSE(runtime.start(scene));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(physics.get_statistics().bodies, 0u);
            EXPECT_EQ(physics.get_statistics().joints, 0u);
            ASSERT_TRUE(actor.try_set_component(valid));
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(actor.try_set_component(value));
            EXPECT_FALSE(runtime.advance(0.01));
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(physics.get_statistics().joints, 0u);
        }
    }

}
