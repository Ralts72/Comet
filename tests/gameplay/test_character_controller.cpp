#include "gameplay/character_controller.h"
#include "input/input_actions.h"
#include "physics/physics_service.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/systems/physics_system.h"
#include "scripting/script_instance.h"

#include <gtest/gtest.h>
#include <array>
#include <cmath>

namespace Comet::Tests {
    class CharacterGameplayTest: public testing::Test {
    protected:
        Scene scene;
        PhysicsService physics;
        SceneRuntime runtime;
        Entity player;
        Input input;
        InputActions actions;
        InputState snapshot;
        Gameplay::CharacterController controller{"strafe", "advance", "hop"};

        void SetUp() override {
            const auto configured = InputActions::create({
                {"strafe", InputActions::Type::Axis, {{Input::Key::D}}},
                {"advance", InputActions::Type::Axis, {{Input::Key::W, -1}}},
                {"hop", InputActions::Type::Button, {{Input::Key::Space}}},
                {"dash", InputActions::Type::Button, {{Input::Key::LeftShift}}},
                {"duck", InputActions::Type::Button, {{Input::Key::LeftControl}}},
            });
            ASSERT_TRUE(configured) << configured.error();
            actions = configured.value();
            input.focus_event(true);
            actions.evaluate(input.publish_frame(), snapshot);
            ASSERT_TRUE(runtime.set_services({.physics = &physics}));
            ASSERT_TRUE(runtime.add_system(std::make_unique<PhysicsSystem>(physics)));
            auto floor = scene.create_entity("Floor");
            floor.edit_transform([](auto& transform) { transform.translation.y = -0.5f; });
            floor.add_component<RigidBodyComponent>(
                RigidBodyComponent{.motion = BodyMotion::Static});
            floor.add_component<ColliderComponent>(
                ColliderComponent{.half_extents = {20, 0.5f, 20}});
            player = scene.create_entity("Player");
            player.add_component<CharacterControllerComponent>();
            ASSERT_TRUE(runtime.start(scene));
            ASSERT_TRUE(runtime.advance(1.0 / 60));
        }

        Result<bool> step(bool enabled = true) {
            actions.evaluate(input.publish_frame(), snapshot);
            const auto controlled = controller.update(scene, player, snapshot, physics, enabled);
            if(!controlled)
                return controlled;
            const auto advanced = runtime.advance(1.0 / 60);
            if(!advanced)
                return Result<bool>::failure(advanced.error().message);
            return controlled;
        }
        Math::Vec3 position() const {
            return player.get_component<TransformComponent>().translation;
        }
    };

    TEST_F(CharacterGameplayTest, CustomActionsMoveDiagonallyAndDisabledOrUnfocusedInputStops) {
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::W, true);
        for(int index = 0; index < 60; ++index)
            ASSERT_TRUE(step());
        EXPECT_NEAR(position().x, 4 / std::sqrt(2.0f), 0.05f);
        EXPECT_NEAR(position().z, -4 / std::sqrt(2.0f), 0.05f);
        const auto stopped = position();
        ASSERT_TRUE(step(false));
        EXPECT_NEAR(position().x, stopped.x, 0.01f);
        EXPECT_NEAR(position().z, stopped.z, 0.01f);
        input.focus_event(false);
        ASSERT_TRUE(step());
        EXPECT_NEAR(position().x, stopped.x, 0.01f);
        EXPECT_NEAR(position().z, stopped.z, 0.01f);
    }

    TEST_F(CharacterGameplayTest, JumpUsesGroundedPressAndHoldingThroughLandingDoesNotJumpAgain) {
        ASSERT_TRUE(physics.get_character_state(player)->grounded);
        input.key_event(Input::Key::Space, true);
        auto jumped = step();
        ASSERT_TRUE(jumped) << jumped.error();
        EXPECT_TRUE(jumped.value());
        EXPECT_GT(position().y, 0.04f);
        for(int index = 0; index < 150; ++index) {
            jumped = step();
            ASSERT_TRUE(jumped) << jumped.error();
            EXPECT_FALSE(jumped.value());
        }
        EXPECT_TRUE(physics.get_character_state(player)->grounded);
        EXPECT_NEAR(position().y, 0, 0.03f);
        input.key_event(Input::Key::Space, false);
        ASSERT_TRUE(step());
        input.key_event(Input::Key::Space, true);
        jumped = step();
        ASSERT_TRUE(jumped) << jumped.error();
        EXPECT_TRUE(jumped.value());
    }

    TEST_F(CharacterGameplayTest, CameraReferencePreservesAnalogSpeedAndHandlesVerticalPitch) {
        auto half_axes = InputActions::create({
            {"strafe", InputActions::Type::Axis, {{Input::Key::D, 0.5f}}},
            {"advance", InputActions::Type::Axis, {{Input::Key::W, -0.5f}}},
            {"hop", InputActions::Type::Button, {{Input::Key::Space}}},
        });
        ASSERT_TRUE(half_axes);
        actions = half_axes.value();
        auto parent = scene.create_entity("Camera parent");
        parent.edit_transform([](auto& transform) {
            transform.rotation.y = 90;
            transform.scale = {2, 3, 4};
        });
        auto camera = scene.create_entity("Camera");
        camera.edit_transform([](auto& transform) { transform.scale = {3, 5, 7}; });
        ASSERT_TRUE(scene.set_parent(camera, parent));
        controller.movement_reference = camera.get_uuid();
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::W, true);
        for(const auto pitch : {0.0f, -60.0f, -90.0f}) {
            SCOPED_TRACE(pitch);
            camera.edit_transform([&](auto& transform) { transform.rotation.x = pitch; });
            player.edit_transform([](auto& transform) { transform.translation = {}; });
            for(int index = 0; index < 60; ++index)
                ASSERT_TRUE(step());
            EXPECT_NEAR(position().x, -2, 0.05f);
            EXPECT_NEAR(position().z, -2, 0.05f);
            EXPECT_NEAR(position().y, 0, 0.03f);
        }
    }

    TEST_F(CharacterGameplayTest, FacingChangesOnlyVisualRotationAndKeepsIdleDirection) {
        auto pivot = scene.create_entity("Visual pivot");
        pivot.edit_transform([](auto& transform) {
            transform.rotation = {20, 35, 15};
            transform.scale = {2, 3, 4};
        });
        ASSERT_TRUE(scene.set_parent(pivot, player));
        auto visual = scene.create_entity("Visual");
        visual.edit_transform([](auto& transform) {
            transform.translation = {0, 1, 0};
            transform.scale = {0.5f, 1.7f, 0.5f};
        });
        ASSERT_TRUE(scene.set_parent(visual, pivot));
        auto camera = scene.create_entity("Camera");
        camera.edit_transform([](auto& transform) { transform.rotation.x = -18; });
        ASSERT_TRUE(scene.set_parent(camera, player));
        controller.facing = visual.get_uuid();
        const auto original = visual.get_component<TransformComponent>();
        input.key_event(Input::Key::D, true);
        ASSERT_TRUE(step());
        (void)scene.get_world_matrix(visual);
        const auto forward =
            -Math::Vec3(visual.get_component<WorldTransformComponent>().pose_world_matrix[2]);
        EXPECT_NEAR(forward.x, 1, 0.0001f);
        EXPECT_NEAR(forward.y, 0, 0.0001f);
        EXPECT_NEAR(forward.z, 0, 0.0001f);
        const auto facing = visual.get_component<TransformComponent>();
        EXPECT_EQ(facing.translation, original.translation);
        EXPECT_EQ(facing.scale, original.scale);
        EXPECT_EQ(player.get_component<TransformComponent>().rotation, Math::Vec3(0));
        EXPECT_EQ(camera.get_component<TransformComponent>().rotation, Math::Vec3(-18, 0, 0));
        input.key_event(Input::Key::D, false);
        ASSERT_TRUE(step());
        ASSERT_TRUE(step(false));
        input.focus_event(false);
        ASSERT_TRUE(step());
        EXPECT_EQ(visual.get_component<TransformComponent>().rotation, facing.rotation);
    }

    TEST_F(CharacterGameplayTest, MissingReferencesAndPhysicsFacingOwnersAreDiagnosed) {
        auto camera = scene.create_entity("Camera");
        controller.movement_reference = camera.get_uuid();
        scene.destroy_entity(camera);
        auto result = controller.update(scene, player, snapshot, physics);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find("reference"), std::string::npos);
        controller.movement_reference = {};
        auto visual = scene.create_entity("Visual");
        controller.facing = visual.get_uuid();
        scene.destroy_entity(visual);
        result = controller.update(scene, player, snapshot, physics);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find("facing"), std::string::npos);
        controller.facing = player.get_uuid();
        result = controller.update(scene, player, snapshot, physics);
        ASSERT_FALSE(result);
        EXPECT_NE(result.error().find("without physics"), std::string::npos);
        auto rigid = scene.create_entity("Rigid visual");
        rigid.add_component<RigidBodyComponent>();
        controller.facing = rigid.get_uuid();
        EXPECT_FALSE(controller.update(scene, player, snapshot, physics));
    }

    TEST_F(CharacterGameplayTest, InvalidActionTypesNamesAndInactiveCharactersAreDiagnosed) {
        auto invalid = controller;
        invalid.move_x = "hop";
        EXPECT_FALSE(invalid.update(scene, player, snapshot, physics));
        invalid = controller;
        invalid.jump = "strafe";
        EXPECT_FALSE(invalid.update(scene, player, snapshot, physics));
        invalid.jump = "missing";
        const auto missing = invalid.update(scene, player, snapshot, physics);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find("missing"), std::string::npos);
        Scene other;
        const auto foreign = other.create_entity();
        EXPECT_FALSE(controller.update(scene, foreign, snapshot, physics));
        EXPECT_FALSE(controller.update(other, player, snapshot, physics));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(controller.update(scene, player, snapshot, physics));
    }

    TEST_F(CharacterGameplayTest, OptionalRunAndCrouchActionsRespectFocusAndControlEnablement) {
        controller.run = "dash";
        controller.crouch = "duck";
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::LeftShift, true);
        for(int index = 0; index < 30; ++index)
            ASSERT_TRUE(step());
        EXPECT_NEAR(position().x, 3, 0.05f);
        EXPECT_EQ(Gameplay::CharacterController::get_state(player, physics).value().motion,
            Gameplay::CharacterController::Motion::Running);
        input.key_event(Input::Key::LeftControl, true);
        for(int index = 0; index < 30; ++index)
            ASSERT_TRUE(step());
        EXPECT_NEAR(position().x, 4, 0.05f);
        EXPECT_TRUE(physics.get_character_state(player)->crouched);
        const auto stopped = position();
        ASSERT_TRUE(step(false));
        EXPECT_NEAR(position().x, stopped.x, 0.01f);
        EXPECT_FALSE(physics.get_character_state(player)->crouched);
        input.focus_event(false);
        ASSERT_TRUE(step());
        EXPECT_FALSE(physics.get_character_state(player)->running);
        controller.run = "strafe";
        input.focus_event(true);
        EXPECT_FALSE(step());
    }

    TEST_F(CharacterGameplayTest, MotionStateReportsAscentDescentAndLandingWithoutOwningASession) {
        using Motion = Gameplay::CharacterController::Motion;
        EXPECT_EQ(
            Gameplay::CharacterController::get_state(player, physics).value().motion, Motion::Idle);
        input.key_event(Input::Key::Space, true);
        ASSERT_TRUE(step());
        EXPECT_EQ(Gameplay::CharacterController::get_state(player, physics).value().motion,
            Motion::Jumping);
        input.key_event(Input::Key::Space, false);
        for(int index = 0; index < 35; ++index)
            ASSERT_TRUE(step());
        EXPECT_EQ(Gameplay::CharacterController::get_state(player, physics).value().motion,
            Motion::Falling);
        for(int index = 0; index < 50; ++index)
            ASSERT_TRUE(step());
        EXPECT_EQ(
            Gameplay::CharacterController::get_state(player, physics).value().motion, Motion::Idle);
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(Gameplay::CharacterController::get_state(player, physics));
    }

    TEST_F(
        CharacterGameplayTest, LuaUsesNativeMovementAndRejectsInvalidOptionsWithoutCorruptingVm) {
        const auto definition = Script::create(R"(
            return {fixed_update = function(self)
                local jumped = comet.control_character(true,
                    {move_x = 'strafe', move_z = 'advance', jump = 'hop'})
                comet.session_set('jump_requested', jumped)
            end}
        )");
        ASSERT_TRUE(definition) << definition.error().message;
        const auto instance = ScriptInstance::create(*definition.value());
        ASSERT_TRUE(instance) << instance.error().message;
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::Space, true);
        actions.evaluate(input.publish_frame(), snapshot);
        const ScriptInstance::Invocation invocation{.scene = &scene,
            .session = &runtime.get_session(),
            .physics = &physics,
            .input = &snapshot};
        ASSERT_TRUE(
            instance.value()->invoke(ScriptInstance::Phase::FixedUpdate, player, {}, invocation));
        EXPECT_EQ(runtime.get_session().get_value("jump_requested"), ParameterValue(true));
        ASSERT_TRUE(runtime.advance(1.0 / 60));
        EXPECT_GT(position().x, 0.04f);
        EXPECT_GT(position().y, 0.04f);

        for(const auto* expression : {"1", "true, {move_x = 7}",
                "true, {move_x = 'invalid action'}", "true, {move_x = 'missing'}",
                "true, {jump = 'strafe'}", "true, {unexpected = 'strafe'}",
                "true, {[1] = 'strafe'}", "true, {['move_x\\0typo'] = 'strafe'}",
                "true, {movement_reference = 'camera'}", "true, {facing = false}"}) {
            SCOPED_TRACE(expression);
            const auto bad_definition =
                Script::create("return {fixed_update = function() comet.control_character("
                               + std::string(expression) + ") end}");
            ASSERT_TRUE(bad_definition) << bad_definition.error().message;
            const auto bad_instance = ScriptInstance::create(*bad_definition.value());
            ASSERT_TRUE(bad_instance) << bad_instance.error().message;
            for(int repeat = 0; repeat < 3; ++repeat) {
                const auto failure = bad_instance.value()->invoke(
                    ScriptInstance::Phase::FixedUpdate, player, {}, invocation);
                ASSERT_FALSE(failure);
                EXPECT_FALSE(failure.error().message.empty());
            }
        }
    }

    TEST_F(
        CharacterGameplayTest, LuaOptionsUseLiveReferencesAndRejectDestroyedOrRecreatedEntities) {
        auto camera = scene.create_entity("Camera");
        camera.edit_transform([](auto& transform) { transform.rotation.y = 90; });
        auto visual = scene.create_entity("Visual");
        ASSERT_TRUE(scene.set_parent(visual, player));
        const auto definition = Script::create(R"(
            local script = {properties = {camera = {type = 'entity'}, visual = {type = 'entity'}}}
            function script:on_start()
                self.controls = {move_x = 'strafe', move_z = 'advance', jump = 'hop',
                    movement_reference = self.parameters.camera, facing = self.parameters.visual}
            end
            function script:fixed_update()
                comet.control_character(true, self.controls)
            end
            return script
        )");
        ASSERT_TRUE(definition) << definition.error().message;
        const auto instance = ScriptInstance::create(*definition.value());
        ASSERT_TRUE(instance) << instance.error().message;
        const ScriptInstance::Invocation invocation{.scene = &scene,
            .session = &runtime.get_session(),
            .physics = &physics,
            .input = &snapshot};
        const ParameterMap parameters{{"camera", camera.get_uuid()}, {"visual", visual.get_uuid()}};
        ASSERT_TRUE(
            instance.value()->invoke(ScriptInstance::Phase::Start, player, parameters, invocation));
        input.key_event(Input::Key::W, true);
        actions.evaluate(input.publish_frame(), snapshot);
        ASSERT_TRUE(instance.value()->invoke(
            ScriptInstance::Phase::FixedUpdate, player, parameters, invocation));
        ASSERT_TRUE(runtime.advance(1.0 / 60));
        EXPECT_LT(position().x, -0.04f);
        EXPECT_NEAR(position().z, 0, 0.001f);
        (void)scene.get_world_matrix(visual);
        EXPECT_NEAR(
            visual.get_component<WorldTransformComponent>().pose_world_matrix[2].x, 1, 0.0001f);
        const auto uuid = camera.get_uuid();
        scene.destroy_entity(camera);
        auto rejected = instance.value()->invoke(
            ScriptInstance::Phase::FixedUpdate, player, parameters, invocation);
        ASSERT_FALSE(rejected);
        EXPECT_NE(rejected.error().message.find("stale"), std::string::npos);
        auto replacement = scene.create_entity_with_uuid(uuid, "Replacement camera");
        ASSERT_TRUE(replacement);
        EXPECT_FALSE(instance.value()->invoke(
            ScriptInstance::Phase::FixedUpdate, player, parameters, invocation));
        ASSERT_TRUE(
            instance.value()->invoke(ScriptInstance::Phase::Start, player, parameters, invocation));
        EXPECT_TRUE(instance.value()->invoke(
            ScriptInstance::Phase::FixedUpdate, player, parameters, invocation));
    }
}
