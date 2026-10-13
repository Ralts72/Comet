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
            const auto controlled = controller.update(player, snapshot, physics, enabled);
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

    TEST_F(CharacterGameplayTest, InvalidActionTypesNamesAndInactiveCharactersAreDiagnosed) {
        auto invalid = controller;
        invalid.move_x = "hop";
        EXPECT_FALSE(invalid.update(player, snapshot, physics));
        invalid = controller;
        invalid.jump = "strafe";
        EXPECT_FALSE(invalid.update(player, snapshot, physics));
        invalid.jump = "missing";
        const auto missing = invalid.update(player, snapshot, physics);
        ASSERT_FALSE(missing);
        EXPECT_NE(missing.error().find("missing"), std::string::npos);
        Scene other;
        const auto foreign = other.create_entity();
        EXPECT_FALSE(controller.update(foreign, snapshot, physics));
        ASSERT_TRUE(runtime.stop());
        EXPECT_FALSE(controller.update(player, snapshot, physics));
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
                "true, {[1] = 'strafe'}", "true, {['move_x\\0typo'] = 'strafe'}"}) {
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
}
