#include "scene/systems/camera_controller.h"
#include "scene/scene.h"

#include <gtest/gtest.h>
#include <cmath>
#include <limits>

namespace Comet::Tests {
    class CameraControllerTest: public testing::Test {
    protected:
        Input input;
        Scene scene;
        Entity entity = scene.create_entity("Camera");
        TransformComponent& camera = entity.get_component<TransformComponent>();

        void SetUp() override {
            input.focus_event(true);
            input.cursor_event({0, 0});
            entity.add_component<CameraComponent>().primary = true;
            entity.add_component<CameraControllerComponent>();
        }

        void update(float delta_time = 0.1f) {
            CameraControllerSystem system;
            EXPECT_TRUE(system.update(scene, {delta_time, 0, 0, input.publish_frame()}));
        }

        void expect_position(Math::Vec3 expected) const {
            EXPECT_NEAR(camera.translation.x, expected.x, 0.00001f);
            EXPECT_NEAR(camera.translation.y, expected.y, 0.00001f);
            EXPECT_NEAR(camera.translation.z, expected.z, 0.00001f);
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
        camera.rotation = {30, -90, 0};
        camera.scale = {2, 3, 4};
        const Math::Vec3 forward{std::sqrt(0.75f), 0.5f, 0};
        input.key_event(Input::Key::W, true);
        update();
        expect_position(forward * 0.3f);

        camera.translation = {};
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::D, true);
        update();
        expect_position({0, 0, 0.3f});

        camera.translation = {};
        input.key_event(Input::Key::D, false);
        input.key_event(Input::Key::E, true);
        update();
        expect_position({0, 0.3f, 0});

        camera.translation = {};
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

        camera.translation = {};
        input.key_event(Input::Key::D, true);
        input.key_event(Input::Key::E, true);
        input.key_event(Input::Key::LeftShift, true);
        update(1);
        EXPECT_NEAR(Math::length(camera.translation), 0.6f, 0.00001f);

        camera.translation = {};
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

    TEST_F(CameraControllerTest, OptInSettingsAndPrimarySelectionDoNotAffectOtherCameras) {
        auto other = scene.create_entity("Other Camera");
        other.add_component<CameraComponent>().primary = true;
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
        entity.get_component<CameraComponent>().primary = false;
        update();
        EXPECT_NE(other.get_component<TransformComponent>().translation, Math::Vec3(0));
        const auto after = other.get_component<TransformComponent>().translation;
        other.remove_component<CameraComponent>();
        update();
        EXPECT_EQ(other.get_component<TransformComponent>().translation, after);
    }

    TEST_F(CameraControllerTest, ParentPoseDrivesDirectionWhileScaleDoesNotChangeWorldSpeed) {
        auto parent = scene.create_entity("Rig");
        auto& rig = parent.get_component<TransformComponent>();
        rig.rotation = {0, -90, 0};
        rig.scale = {2, 3, 4};
        ASSERT_TRUE(scene.set_parent(entity, parent));
        input.key_event(Input::Key::W, true);
        update();
        auto world = Math::Vec3(scene.get_world_matrix(entity)[3]);
        EXPECT_NEAR(world.x, 0.3f, 0.00001f);
        EXPECT_NEAR(world.y, 0, 0.00001f);
        EXPECT_NEAR(world.z, 0, 0.00001f);
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::E, true);
        rig.rotation = {25, -90, 30};
        camera.translation = {};
        update();
        world = Math::Vec3(scene.get_world_matrix(entity)[3]);
        EXPECT_NEAR(world.x, 0, 0.00001f);
        EXPECT_NEAR(world.y, 0.3f, 0.00001f);
        EXPECT_NEAR(world.z, 0, 0.00001f);
        const auto before = camera.translation;
        rig.scale = {};
        update();
        EXPECT_EQ(camera.translation, before);
        EXPECT_TRUE(Math::is_finite(camera.translation));
    }
}
