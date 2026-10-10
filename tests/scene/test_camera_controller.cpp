#include "scene/runtime_session.h"
#include "scene/systems/camera_controller.h"
#include "scene/scene.h"
#include "core/project.h"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

namespace Comet::Tests {
    class CameraControllerTest: public testing::Test {
    protected:
        Input input;
        Scene scene;
        Entity entity = scene.create_entity("Camera");
        const TransformComponent& camera = entity.get_component<TransformComponent>();
        InputActions actions;
        InputState input_state;
        RuntimeSession session;

        void SetUp() override {
            const auto project = Project::load(
                std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "demo");
            ASSERT_TRUE(project) << project.error();
            actions = project.value().input_actions();
            input.focus_event(true);
            input.cursor_event({0, 0});
            entity.add_component<CameraComponent>(CameraComponent{.primary = true});
            entity.add_component<CameraControllerComponent>();
        }

        void update(float delta_time = 0.1f) {
            CameraControllerSystem system;
            const auto& frame = input.publish_frame();
            actions.evaluate(frame, input_state);
            EXPECT_TRUE(system.update(scene, {delta_time, 0, 0, input_state, session}));
        }

        void expect_position(Math::Vec3 expected) const {
            EXPECT_NEAR(camera.translation.x, expected.x, 0.00001f);
            EXPECT_NEAR(camera.translation.y, expected.y, 0.00001f);
            EXPECT_NEAR(camera.translation.z, expected.z, 0.00001f);
        }

        void expect_rotation(Math::Vec3 expected) const {
            EXPECT_NEAR(camera.rotation.x, expected.x, 0.00001f);
            EXPECT_NEAR(camera.rotation.y, expected.y, 0.00001f);
            EXPECT_NEAR(camera.rotation.z, expected.z, 0.00001f);
        }
    };

    TEST_F(CameraControllerTest, TurnsOnlyDuringRightDragAndSkipsThePressFrame) {
        input.cursor_event({100, 200});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(0));
        input.cursor_event({300, 400});
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(0));
        input.cursor_event({350, 380});
        update(0);
        EXPECT_EQ(camera.rotation, Math::Vec3(4, -10, 0));
        input.mouse_button_event(Input::MouseButton::Right, false);
        input.cursor_event({600, 600});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(4, -10, 0));
        expect_position(Math::Vec3(0));
    }

    TEST_F(CameraControllerTest, GamepadLookUsesRuntimeTimeWithoutMouseCapture) {
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 1;
        input.gamepad_sample(0, pad);
        update(0.2f);
        const auto single_frame = camera.rotation;
        expect_rotation({0, -24, 0});
        EXPECT_FALSE(CameraControllerSystem{}.wants_cursor_capture(scene, input_state));
        ASSERT_TRUE(entity.try_edit_transform([](auto& value) { value.rotation = {}; }));
        update(0.1f);
        update(0.1f);
        expect_rotation(single_frame);

        auto& controller = entity.get_component<CameraControllerComponent>();
        controller.look_speed = 60;
        update(0.1f);
        expect_rotation({0, -30, 0});
        for(const float invalid : {-1.0f, std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::quiet_NaN()}) {
            controller.look_speed = invalid;
            update();
            expect_rotation({0, -30, 0});
        }
    }

    TEST_F(CameraControllerTest, MouseAndGamepadLookConvertBeforeClamping) {
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 1;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightY)] = 1;
        input.gamepad_sample(0, pad);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.cursor_event({40, -60});
        update();
        // 开始拖动只跳过鼠标位移，不跳过这一帧的手柄角速度。
        expect_rotation({-12, -12, 0});
        ASSERT_TRUE(entity.try_edit_transform([](auto& value) { value.rotation = {80, 175, 0}; }));
        input.cursor_event({-60, -160});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(88, -177, 0));

        ASSERT_TRUE(entity.try_edit_transform([](auto& value) { value.rotation = {}; }));
        input.cursor_event({-10, -140});
        update(0);
        EXPECT_EQ(camera.rotation, Math::Vec3(-4, -10, 0));
    }

    TEST_F(CameraControllerTest, GamepadLookHonorsDeadzoneInversionAndDisconnect) {
        const auto configured =
            InputActions::create({{"camera.look_rate_x", InputActions::Type::Axis,
                                      {{Input::GamepadAxis::RightX, -1, 0.25f}}},
                {"camera.look_rate_y", InputActions::Type::Axis,
                    {{Input::GamepadAxis::RightY, 1, 0.25f}}}});
        ASSERT_TRUE(configured);
        actions = configured.value();
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 0.2f;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightY)] = -0.25f;
        input.gamepad_sample(0, pad);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(0));
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 0.625f;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightY)] = -0.625f;
        input.gamepad_sample(0, pad);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(6, 6, 0));
        input.gamepad_sample(0, std::nullopt);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(6, 6, 0));
        input.gamepad_sample(0, pad);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(12, 12, 0));
        input.focus_event(false);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(12, 12, 0));
    }

    TEST_F(CameraControllerTest, PointerAuthorizationDoesNotBlockGamepadLook) {
        Input::Gate gate;
        CameraControllerSystem system;
        const auto advance = [&](bool enabled, bool pointer_enabled) {
            actions.evaluate(
                gate.read(input.publish_frame(), enabled, pointer_enabled), input_state);
            EXPECT_TRUE(system.update(scene, {0.1, 0, 0, input_state, session}));
        };
        advance(true, true);
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::RightX)] = 1;
        input.gamepad_sample(0, pad);
        input.mouse_button_event(Input::MouseButton::Right, true);
        advance(true, true);
        expect_rotation({0, -12, 0});
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        input.cursor_event({100, 100});
        advance(true, false);
        expect_rotation({0, -24, 0});
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        advance(false, false);
        expect_rotation({0, -24, 0});
    }

    TEST_F(CameraControllerTest, CaptureFollowsTheAuthorizedLookActionIncludingItsPressFrame) {
        CameraControllerSystem system;
        update();
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        ASSERT_TRUE(input_state.action("camera.look")->pressed);
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        EXPECT_EQ(camera.rotation, Math::Vec3(0));
        input.mouse_button_event(Input::MouseButton::Right, false);
        update();
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));

        const auto rebound =
            InputActions::create({{"camera.look", InputActions::Type::Button, {{Input::Key::K}}}});
        ASSERT_TRUE(rebound);
        actions = rebound.value();
        input_state = {};
        input.key_event(Input::Key::K, true);
        update();
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        auto frame = input.publish_frame();
        frame.pointer_enabled = false;
        actions.evaluate(frame, input_state);
        ASSERT_TRUE(input_state.action("camera.look")->down);
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        input.focus_event(false);
        update();
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));

        input.focus_event(true);
        input.key_event(Input::Key::K, true);
        const auto wrong_type =
            InputActions::create({{"camera.look", InputActions::Type::Axis, {{Input::Key::K}}}});
        ASSERT_TRUE(wrong_type);
        input_state = {};
        wrong_type.value().evaluate(input.publish_frame(), input_state);
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
    }

    TEST_F(CameraControllerTest, CaptureUsesCurrentPrimaryControllerAndParentValidity) {
        CameraControllerSystem system;
        auto other = scene.create_entity("Other camera");
        other.add_component<CameraComponent>(CameraComponent{.primary = true});
        other.add_component<CameraControllerComponent>();
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        auto& controller = entity.get_component<CameraControllerComponent>();
        controller.enabled = false;
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        controller.enabled = true;
        controller.look_sensitivity = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        controller.look_sensitivity = 0.2f;
        auto parent = scene.create_entity("Rig");
        ASSERT_TRUE(scene.set_parent(entity, parent));
        parent.edit_transform([](auto& transform) { transform.scale = {}; });
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        parent.edit_transform([](auto& transform) { transform.scale = Math::Vec3(1); });
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        entity.remove_component<CameraControllerComponent>();
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
        entity.edit_component<CameraComponent>([&](auto& component) { component.primary = false; });
        EXPECT_TRUE(system.wants_cursor_capture(scene, input_state));
        scene.destroy_entity(other);
        EXPECT_FALSE(system.wants_cursor_capture(scene, input_state));
    }

    TEST_F(CameraControllerTest, ClampsPitchWrapsYawAndRebaselinesAfterFocusLoss) {
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        input.cursor_event({2000, -1000});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(89, -40, 0));
        input.cursor_event({2000, 1000});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(-89, -40, 0));
        input.focus_event(false);
        input.cursor_event({4000, 4000});
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 5});
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(-89, -40, 0));
        expect_position(Math::Vec3(0));
        input.focus_event(true);
        input.cursor_event({6000, 6000});
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        EXPECT_EQ(camera.rotation, Math::Vec3(-89, -40, 0));
    }

    TEST_F(CameraControllerTest, MovesAlongCameraAxesButKeepsVerticalMovementInWorldSpace) {
        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.rotation = {30, -90, 0}; }));
        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.scale = {2, 3, 4}; }));
        const Math::Vec3 forward{std::sqrt(0.75f), 0.5f, 0};
        input.key_event(Input::Key::W, true);
        update();
        expect_position(forward * 0.3f);

        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::D, true);
        update();
        expect_position({0, 0, 0.3f});

        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        input.key_event(Input::Key::D, false);
        input.key_event(Input::Key::E, true);
        update();
        expect_position({0, 0.3f, 0});

        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        input.key_event(Input::Key::E, false);
        input.scroll_event({0, 1});
        update(0);
        expect_position(forward * 0.2f);
    }

    TEST_F(CameraControllerTest, MovesUsingTheNewOrientationAndBoundsCombinedMovement) {
        input.mouse_button_event(Input::MouseButton::Right, true);
        update();
        input.cursor_event({450, 0});
        input.key_event(Input::Key::W, true);
        update();
        expect_position({0.3f, 0, 0});

        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::E, true);
        input.key_event(Input::Key::LeftShift, true);
        update(1);
        EXPECT_NEAR(Math::length(camera.translation), 6.0f, 0.00001f);

        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::D, false);
        input.key_event(Input::Key::E, false);
        input.key_event(Input::Key::LeftShift, false);
        Input::GamepadSample pad;
        pad.axes[static_cast<size_t>(Input::GamepadAxis::LeftY)] = -1;
        input.gamepad_sample(0, pad);
        update();
        expect_position({0.3f, 0, 0});
    }

    TEST_F(CameraControllerTest, MovementUsesAllRuntimeTimeRegardlessOfFramePartition) {
        input.key_event(Input::Key::W, true);
        update(0.2f);
        const auto single_frame = camera.translation;
        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        update(0.1f);
        update(0.1f);
        expect_position(single_frame);
        expect_position({0, 0, -0.6f});
    }

    TEST_F(CameraControllerTest, OptInSettingsAndPrimarySelectionDoNotAffectOtherCameras) {
        auto other = scene.create_entity("Other Camera");
        other.add_component<CameraComponent>(CameraComponent{.primary = true});
        other.add_component<CameraControllerComponent>();
        input.key_event(Input::Key::W, true);
        auto& controller = entity.get_component<CameraControllerComponent>();
        controller.enabled = false;
        update();
        expect_position({});
        EXPECT_EQ(other.get_component<TransformComponent>().translation, Math::Vec3(0));
        controller.enabled = true;
        controller.move_speed = 8;
        controller.look_sensitivity = 0.5f;
        update();
        expect_position({0, 0, -0.8f});
        input.mouse_button_event(Input::MouseButton::Right, true);
        update(0);
        input.cursor_event({20, -10});
        update(0);
        EXPECT_EQ(camera.rotation, Math::Vec3(5, -10, 0));
        const auto before = camera.translation;
        controller.move_speed = std::numeric_limits<float>::infinity();
        update();
        EXPECT_EQ(camera.translation, before);
        entity.remove_component<CameraControllerComponent>();
        update();
        EXPECT_EQ(camera.translation, before);
        EXPECT_EQ(other.get_component<TransformComponent>().translation, Math::Vec3(0));
        entity.edit_component<CameraComponent>([&](auto& component) { component.primary = false; });
        update();
        EXPECT_NE(other.get_component<TransformComponent>().translation, Math::Vec3(0));
        const auto after = other.get_component<TransformComponent>().translation;
        other.remove_component<CameraComponent>();
        update();
        EXPECT_EQ(other.get_component<TransformComponent>().translation, after);
    }

    TEST_F(CameraControllerTest, ParentPoseDrivesDirectionWhileScaleDoesNotChangeWorldSpeed) {
        auto parent = scene.create_entity("Rig");
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.rotation = {0, -90, 0}; }));
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.scale = {2, 3, 4}; }));
        ASSERT_TRUE(scene.set_parent(entity, parent));
        input.key_event(Input::Key::W, true);
        update();
        auto world = Math::Vec3(scene.get_world_matrix(entity)[3]);
        EXPECT_NEAR(world.x, 0.3f, 0.00001f);
        EXPECT_NEAR(world.y, 0, 0.00001f);
        EXPECT_NEAR(world.z, 0, 0.00001f);
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::E, true);
        EXPECT_TRUE(
            parent.try_edit_transform([&](auto& value) { value.rotation = {25, -90, 30}; }));
        EXPECT_TRUE(entity.try_edit_transform([&](auto& value) { value.translation = {}; }));
        update();
        world = Math::Vec3(scene.get_world_matrix(entity)[3]);
        EXPECT_NEAR(world.x, 0, 0.00001f);
        EXPECT_NEAR(world.y, 0.3f, 0.00001f);
        EXPECT_NEAR(world.z, 0, 0.00001f);
        const auto before = camera.translation;
        EXPECT_TRUE(parent.try_edit_transform([&](auto& value) { value.scale = {}; }));
        update();
        EXPECT_EQ(camera.translation, before);
        EXPECT_TRUE(Math::is_finite(camera.translation));
    }

    TEST_F(CameraControllerTest, RebindingChangesControlsWithoutChangingTheSystem) {
        auto rebound = InputActions::create(
            {{"camera.move_z", InputActions::Type::Axis, {{Input::Key::Up, -1}}}});
        ASSERT_TRUE(rebound);
        actions = std::move(rebound).value();
        input_state = {};
        input.key_event(Input::Key::W, true);
        update();
        expect_position({});
        input.key_event(Input::Key::Up, true);
        update();
        expect_position({0, 0, -0.3f});
    }
}
