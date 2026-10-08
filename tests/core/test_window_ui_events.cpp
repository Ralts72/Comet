#include "core/window.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include <limits>

namespace Comet {
    namespace {
        class WindowUiEventsTest: public ::testing::Test {
        protected:
            Window window{WindowSettings{.width = 320, .height = 240, .title = "UI events"}};
            GLFWkeyfun key = nullptr;
            GLFWcharfun character = nullptr;
            GLFWcursorposfun cursor = nullptr;
            GLFWmousebuttonfun mouse = nullptr;
            GLFWscrollfun scroll = nullptr;
            GLFWwindowfocusfun focus = nullptr;

            void SetUp() override {
                window.poll_events();
                key = glfwSetKeyCallback(window.get(), nullptr);
                glfwSetKeyCallback(window.get(), key);
                character = glfwSetCharCallback(window.get(), nullptr);
                glfwSetCharCallback(window.get(), character);
                cursor = glfwSetCursorPosCallback(window.get(), nullptr);
                glfwSetCursorPosCallback(window.get(), cursor);
                mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
                glfwSetMouseButtonCallback(window.get(), mouse);
                scroll = glfwSetScrollCallback(window.get(), nullptr);
                glfwSetScrollCallback(window.get(), scroll);
                focus = glfwSetWindowFocusCallback(window.get(), nullptr);
                glfwSetWindowFocusCallback(window.get(), focus);
                ASSERT_TRUE(key && character && cursor && mouse && scroll && focus);
                ASSERT_EQ(glfwGetWindowUserPointer(window.get()), &window);
                focus(window.get(), GLFW_FALSE);
                focus(window.get(), GLFW_TRUE);
                window.discard_pending_input();
                window.publish_input_frame();
            }
        };
    }

    TEST_F(WindowUiEventsTest, PublishesOrderedRepeatAndUnicodeWithoutConsumingPhysicalInput) {
        constexpr int modifiers = GLFW_MOD_CONTROL | GLFW_MOD_SHIFT | GLFW_MOD_SUPER
                                  | GLFW_MOD_CAPS_LOCK | GLFW_MOD_NUM_LOCK;
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, modifiers);
        key(window.get(), GLFW_KEY_A, 0, GLFW_REPEAT, modifiers);
        character(window.get(), U'你');
        cursor(window.get(), 12.5, 24.25);
        const Math::Vec2 button_position{12.5f, 24.25f};
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, modifiers);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, modifiers);
        scroll(window.get(), 0.5, -2);
        key(window.get(), GLFW_KEY_A, 0, GLFW_RELEASE, modifiers);
        EXPECT_TRUE(window.get_ui_events().empty());

        const auto frame = window.publish_input_frame();
        const auto events = window.get_ui_events();
        ASSERT_EQ(events.size(), 8u);
        EXPECT_EQ(events[0].type, Window::UiEvent::Type::KeyDown);
        EXPECT_FALSE(events[0].repeat);
        EXPECT_EQ(events[1].key, Input::Key::A);
        EXPECT_TRUE(events[1].repeat);
        EXPECT_EQ(events[2].type, Window::UiEvent::Type::Text);
        EXPECT_EQ(events[2].codepoint, U'你');
        EXPECT_EQ(events[2].modifiers, Window::UiEvent::Control | Window::UiEvent::Shift
                                           | Window::UiEvent::Super | Window::UiEvent::CapsLock
                                           | Window::UiEvent::NumLock);
        EXPECT_EQ(events[3].position, Math::Vec2(12.5f, 24.25f));
        EXPECT_EQ(events[4].type, Window::UiEvent::Type::MouseDown);
        EXPECT_EQ(events[4].position, button_position);
        EXPECT_EQ(events[5].type, Window::UiEvent::Type::MouseUp);
        EXPECT_EQ(events[5].position, button_position);
        EXPECT_EQ(events[6].position, Math::Vec2(0.5f, -2));
        EXPECT_EQ(events[7].type, Window::UiEvent::Type::KeyUp);
        EXPECT_TRUE(frame.key(Input::Key::A).pressed);
        EXPECT_TRUE(frame.key(Input::Key::A).released);
        EXPECT_FALSE(frame.key(Input::Key::A).down);
        EXPECT_EQ(window.get_ui_events().data(), events.data());
        window.publish_input_frame();
        EXPECT_TRUE(window.get_ui_events().empty());
        EXPECT_FALSE(window.get_input_frame().key(Input::Key::A).pressed);
        EXPECT_FALSE(window.get_input_frame().key(Input::Key::A).released);
    }

    TEST_F(WindowUiEventsTest, ButtonCoordinatesFollowTheOrderedCursorEvents) {
        cursor(window.get(), 12.5, 24.25);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        cursor(window.get(), 80, 90);
        cursor(window.get(), std::numeric_limits<double>::quiet_NaN(), 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        const auto& frame = window.publish_input_frame();
        const auto events = window.get_ui_events();
        ASSERT_EQ(events.size(), 4u);
        EXPECT_EQ(events[1].type, Window::UiEvent::Type::MouseDown);
        EXPECT_EQ(events[1].position, Math::Vec2(12.5f, 24.25f));
        EXPECT_EQ(events[3].type, Window::UiEvent::Type::MouseUp);
        EXPECT_EQ(events[3].position, Math::Vec2(80, 90));
        EXPECT_EQ(frame.cursor_position, events[3].position);
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).pressed);
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).released);
    }

    TEST_F(WindowUiEventsTest, RejectsInvalidUnicodeAndCoordinatesAndUnfocusedText) {
        character(window.get(), 0);
        character(window.get(), 0xd800);
        character(window.get(), 0x110000);
        cursor(window.get(), std::numeric_limits<double>::infinity(), 0);
        scroll(window.get(), 0, std::numeric_limits<double>::quiet_NaN());
        character(window.get(), U'😀');
        window.publish_input_frame();
        ASSERT_EQ(window.get_ui_events().size(), 1u);
        EXPECT_EQ(window.get_ui_events()[0].codepoint, U'😀');

        const auto interruption = window.get_input_frame().interruption;
        focus(window.get(), GLFW_FALSE);
        character(window.get(), U'中');
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        focus(window.get(), GLFW_FALSE);
        window.publish_input_frame();
        ASSERT_EQ(window.get_ui_events().size(), 1u);
        EXPECT_EQ(window.get_ui_events()[0].type, Window::UiEvent::Type::Focus);
        EXPECT_FALSE(window.get_ui_events()[0].focused);
        EXPECT_EQ(window.get_input_frame().interruption, interruption + 1);
    }

    TEST_F(WindowUiEventsTest, OverflowCancelsWholeBatchOnceAndNextBatchRecovers) {
        const auto interruption = window.get_input_frame().interruption;
        for(size_t index = 0; index < Window::MAX_UI_EVENTS * 2; ++index)
            character(window.get(), U'x');
        window.publish_input_frame();
        EXPECT_TRUE(window.get_ui_events().empty());
        EXPECT_EQ(window.get_input_frame().interruption, interruption + 1);
        character(window.get(), U'新');
        window.publish_input_frame();
        ASSERT_EQ(window.get_ui_events().size(), 1u);
        EXPECT_EQ(window.get_ui_events()[0].codepoint, U'新');
        EXPECT_EQ(window.get_input_frame().interruption, interruption + 1);
    }

    TEST_F(WindowUiEventsTest, DiscardDropsUnpublishedTextAndExposesLogicalSizeAndScale) {
        character(window.get(), U'旧');
        const auto interruption = window.get_input_frame().interruption;
        window.discard_pending_input();
        character(window.get(), U'新');
        window.publish_input_frame();
        ASSERT_EQ(window.get_ui_events().size(), 1u);
        EXPECT_EQ(window.get_ui_events()[0].codepoint, U'新');
        EXPECT_EQ(window.get_input_frame().interruption, interruption + 1);
        EXPECT_EQ(window.get_size(), Math::Vec2u(320, 240));
        const auto scale = window.get_content_scale();
        EXPECT_TRUE(Math::is_finite(scale));
        EXPECT_GT(scale.x, 0);
        EXPECT_GT(scale.y, 0);
        EXPECT_EQ(glfwGetWindowUserPointer(window.get()), &window);
    }
}
