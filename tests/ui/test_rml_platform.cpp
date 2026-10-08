#include "ui/rml_platform.h"

#include "common/scope_exit.h"
#include "core/window.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <gtest/gtest.h>

namespace Comet::Ui {
    namespace {
        class NullRenderer final: public Rml::RenderInterface {
        public:
            Rml::CompiledGeometryHandle CompileGeometry(
                Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override {
                return 1;
            }
            void RenderGeometry(
                Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
            void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
            Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override {
                return 0;
            }
            Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
                return 1;
            }
            void ReleaseTexture(Rml::TextureHandle) override {}
            void EnableScissorRegion(bool) override {}
            void SetScissorRegion(Rml::Rectanglei) override {}
        };

        class EventCounter final: public Rml::EventListener {
        public:
            int key_down = 0;
            int key_up = 0;
            int clicks = 0;
            RmlPlatform* platform = nullptr;
            Rml::Context* context = nullptr;

            void ProcessEvent(Rml::Event& event) override {
                if(event.GetType() == "click")
                    ++clicks;
                if(event.GetType() == "keyup")
                    ++key_up;
                if(event.GetType() != "keydown")
                    return;
                ++key_down;
                if(platform
                    && event.GetParameter<int>("key_identifier", 0) == Rml::Input::KI_ESCAPE)
                    platform->cancel_input(*context);
            }
        };

        class RmlPlatformTest: public ::testing::Test {
        protected:
            Comet::Window window{
                Comet::WindowSettings{.width = 320, .height = 240, .title = "Rml platform"}};
            NullRenderer renderer;
            RmlPlatform platform;
            Rml::Context* context = nullptr;
            Rml::ElementDocument* document = nullptr;
            GLFWkeyfun key = nullptr;
            GLFWcharfun character = nullptr;
            GLFWwindowfocusfun focus = nullptr;
            GLFWcursorposfun cursor = nullptr;
            GLFWmousebuttonfun mouse = nullptr;

            void SetUp() override {
                glfwFocusWindow(window.get());
                window.poll_events();
                key = glfwSetKeyCallback(window.get(), nullptr);
                glfwSetKeyCallback(window.get(), key);
                character = glfwSetCharCallback(window.get(), nullptr);
                glfwSetCharCallback(window.get(), character);
                focus = glfwSetWindowFocusCallback(window.get(), nullptr);
                glfwSetWindowFocusCallback(window.get(), focus);
                cursor = glfwSetCursorPosCallback(window.get(), nullptr);
                glfwSetCursorPosCallback(window.get(), cursor);
                mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
                glfwSetMouseButtonCallback(window.get(), mouse);
                ASSERT_TRUE(key && character && focus && cursor && mouse);
                focus(window.get(), GLFW_FALSE);
                focus(window.get(), GLFW_TRUE);
                window.discard_pending_input();
                Rml::SetRenderInterface(&renderer);
                ASSERT_TRUE(Rml::Initialise());
                context = Rml::CreateContext("platform-test", {320, 240});
                ASSERT_NE(context, nullptr);
                document = context->LoadDocumentFromMemory(R"(
                    <rml><head><style>
                    body { width: 100%; height: 100%; }
                    input, button {
                        display: block; width: 100dp; height: 30dp; tab-index: auto;
                        nav-up: auto; nav-down: auto; nav-left: auto; nav-right: auto;
                    }
                    </style></head><body>
                    <input id="text" type="text" />
                    <button id="first"></button><button id="second"></button>
                    </body></rml>)");
                ASSERT_NE(document, nullptr);
                document->Show();
                tick();
                tick();
                context->Update();
                ASSERT_EQ(glfwGetWindowUserPointer(window.get()), &window);
            }

            void TearDown() override {
                if(context)
                    Rml::RemoveContext(context->GetName());
                Rml::Shutdown();
                Rml::SetRenderInterface(nullptr);
            }

            void tick(bool open = true) {
                platform.update(*context, window, window.publish_input_frame(), open);
            }

            void move_cursor(double x, double y) {
                glfwSetCursorPos(window.get(), x, y);
                cursor(window.get(), x, y);
            }

            Rml::ElementFormControlInput* text() {
                return dynamic_cast<Rml::ElementFormControlInput*>(
                    document->GetElementById("text"));
            }
        };
    }

    TEST_F(RmlPlatformTest, UnicodeAndRepeatAreDeliveredOnceAndCaptureKeepsFocusWithoutText) {
        ASSERT_NE(text(), nullptr);
        ASSERT_TRUE(text()->Focus());
        EventCounter counter;
        context->AddEventListener("keydown", &counter, true);
        context->AddEventListener("keyup", &counter, true);
        context->AddEventListener("click", &counter, true);
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        character(window.get(), U'中');
        character(window.get(), U'😀');
        key(window.get(), GLFW_KEY_A, 0, GLFW_REPEAT, 0);
        key(window.get(), GLFW_KEY_A, 0, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(text()->GetValue(), "中😀");
        EXPECT_TRUE(platform.text_input_active());
        EXPECT_EQ(counter.key_down, 2);
        EXPECT_EQ(counter.key_up, 1);
        platform.update(*context, window, window.get_input_frame(), true);
        EXPECT_EQ(text()->GetValue(), "中😀");
        EXPECT_EQ(counter.key_down, 2);

        platform.set_capture_active(true);
        tick();
        EXPECT_EQ(context->GetFocusElement(), text());
        character(window.get(), U'旧');
        key(window.get(), GLFW_KEY_B, 0, GLFW_PRESS, 0);
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(text()->GetValue(), "中😀");
        EXPECT_EQ(context->GetFocusElement(), text());
        EXPECT_EQ(counter.clicks, 0);
        EXPECT_FALSE(platform.text_input_active());
        EXPECT_EQ(counter.key_down, 2);
        context->RemoveEventListener("keydown", &counter, true);
        context->RemoveEventListener("keyup", &counter, true);
        context->RemoveEventListener("click", &counter, true);
    }

    TEST_F(RmlPlatformTest, CloseDuringDispatchDropsRemainingTextAndHeldKeys) {
        ASSERT_TRUE(text()->Focus());
        EventCounter counter;
        counter.platform = &platform;
        counter.context = context;
        context->AddEventListener("keydown", &counter, true);
        context->AddEventListener("keyup", &counter, true);
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        character(window.get(), U'a');
        key(window.get(), GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
        character(window.get(), U'z');
        key(window.get(), GLFW_KEY_B, 0, GLFW_PRESS, 0);
        tick();
        EXPECT_EQ(text()->GetValue(), "a");
        EXPECT_EQ(counter.key_down, 2);
        EXPECT_EQ(counter.key_up, 2);
        EXPECT_FALSE(platform.text_input_active());
        tick(false);
        EXPECT_EQ(counter.key_up, 2);
        context->RemoveEventListener("keydown", &counter, true);
        context->RemoveEventListener("keyup", &counter, true);
    }

    TEST_F(RmlPlatformTest, FocusRecoveryAcceptsFreshOrderedTapsWithoutLeakingHeldRepeat) {
        auto* first = document->GetElementById("first");
        auto* second = document->GetElementById("second");
        ASSERT_TRUE(first->Focus());
        EventCounter counter;
        context->AddEventListener("keydown", &counter, true);
        context->AddEventListener("keyup", &counter, true);
        second->AddEventListener("click", &counter);
        const ScopeExit detach([&] {
            context->RemoveEventListener("keydown", &counter, true);
            context->RemoveEventListener("keyup", &counter, true);
            second->RemoveEventListener("click", &counter);
        });
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        tick();
        ASSERT_EQ(counter.key_down, 1);

        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        tick();
        EXPECT_EQ(counter.key_down, 1);
        EXPECT_EQ(counter.key_up, 1);
        EXPECT_NE(context->GetFocusElement(), first);
        ASSERT_TRUE(first->Focus());

        key(window.get(), GLFW_KEY_A, 0, GLFW_REPEAT, 0);
        key(window.get(), GLFW_KEY_TAB, 0, GLFW_PRESS, 0);
        key(window.get(), GLFW_KEY_TAB, 0, GLFW_RELEASE, 0);
        key(window.get(), GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
        key(window.get(), GLFW_KEY_ENTER, 0, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(context->GetFocusElement(), second);
        EXPECT_EQ(counter.key_down, 3);
        EXPECT_EQ(counter.key_up, 3);
        EXPECT_EQ(counter.clicks, 1);
        EXPECT_TRUE(window.get_input_frame().key(Comet::Input::Key::A).down);
        EXPECT_FALSE(window.get_input_frame().key(Comet::Input::Key::A).pressed);

        key(window.get(), GLFW_KEY_A, 0, GLFW_RELEASE, 0);
        key(window.get(), GLFW_KEY_A, 0, GLFW_PRESS, 0);
        key(window.get(), GLFW_KEY_A, 0, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(counter.key_down, 4);
        EXPECT_EQ(counter.key_up, 4);
        EXPECT_EQ(counter.clicks, 1);
    }

    TEST_F(RmlPlatformTest, ClosedHudAcceptsPointerWhileKeyboardAndTextRemainUnowned) {
        tick(false);
        tick(false);
        auto* button = document->GetElementById("first");
        EventCounter counter;
        button->AddEventListener("click", &counter);
        key(window.get(), GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
        key(window.get(), GLFW_KEY_ENTER, 0, GLFW_RELEASE, 0);
        character(window.get(), U'旧');
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick(false);
        EXPECT_EQ(counter.clicks, 1);
        EXPECT_TRUE(text()->GetValue().empty());
        EXPECT_FALSE(platform.text_input_active());
        button->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, ViewMapsWindowCoordinatesAndRejectsClippedClicksAndOutsideReleases) {
        View view{.origin = {100, 50},
            .size = {160, 120},
            .pixel_size = {320, 240},
            .density = 1,
            .clip = View::Clip{{120, 50}, {140, 120}}};
        const auto tick_view = [&] {
            platform.update(*context, window, window.publish_input_frame(), false, view);
            context->Update();
        };
        tick_view();
        tick_view();
        EXPECT_EQ(context->GetDimensions(), Rml::Vector2i(320, 240));
        auto* button = document->GetElementById("first");
        EventCounter counter;
        button->AddEventListener("click", &counter);
        const ScopeExit detach([&] { button->RemoveEventListener("click", &counter); });

        // 视口半尺寸显示，窗口 (130, 70) 对应 UI 像素 (60, 40)。
        move_cursor(130, 70);
        tick_view();
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick_view();
        EXPECT_EQ(counter.clicks, 1);

        move_cursor(105, 70);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick_view();
        EXPECT_EQ(counter.clicks, 1);
        move_cursor(130, 70);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tick_view();
        move_cursor(105, 70);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick_view();
        EXPECT_EQ(counter.clicks, 1);

        view.pixel_size = {640, 480};
        view.density = 2;
        tick_view();
        move_cursor(130, 70);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick_view();
        EXPECT_EQ(counter.clicks, 2);
        EXPECT_EQ(context->GetDimensions(), Rml::Vector2i(640, 480));
    }

    TEST_F(RmlPlatformTest, LockedCursorKeepsHudPointerUnownedAndGameClickAvailable) {
        glfwFocusWindow(window.get());
        for(int attempt = 0; attempt < 50; ++attempt) {
            const bool focused = glfwGetWindowAttrib(window.get(), GLFW_FOCUSED) == GLFW_TRUE;
            window.poll_events();
            if(focused)
                break;
            window.wait_events(0.01);
        }
        ASSERT_EQ(glfwGetWindowAttrib(window.get(), GLFW_FOCUSED), GLFW_TRUE);
        tick(false);
        tick(false);
        auto* button = document->GetElementById("first");
        EventCounter counter;
        button->AddEventListener("click", &counter);
        move_cursor(10, 40);
        tick(false);
        ASSERT_TRUE(context->IsMouseInteracting());

        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        Comet::Input::Gate game_input;
        for(int frame = 0; frame < 2; ++frame) {
            tick(false);
            game_input.read(window.get_input_frame(), true, !context->IsMouseInteracting());
        }
        EXPECT_FALSE(context->IsMouseInteracting());
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick(false);
        EXPECT_EQ(counter.clicks, 0);
        EXPECT_FALSE(context->IsMouseInteracting());
        const auto& physical = window.get_input_frame();
        EXPECT_TRUE(physical.mouse(Comet::Input::MouseButton::Left).pressed);
        const auto& game = game_input.read(physical, true, !context->IsMouseInteracting());
        EXPECT_TRUE(game.pointer_enabled);
        EXPECT_TRUE(game.mouse(Comet::Input::MouseButton::Left).pressed);

        window.set_cursor_locked(false);
        tick(false);
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick(false);
        EXPECT_EQ(counter.clicks, 1);
        button->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, InterruptedMousePressCancelsWithoutClickAndFreshClickRecovers) {
        auto* button = document->GetElementById("first");
        ASSERT_NE(button, nullptr);
        EventCounter counter;
        button->AddEventListener("click", &counter);
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tick();
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        tick();
        EXPECT_EQ(counter.clicks, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(counter.clicks, 1);
        EXPECT_EQ(context->GetDimensions().x, window.get_framebuffer_size().x);
        EXPECT_EQ(context->GetDimensions().y, window.get_framebuffer_size().y);
        EXPECT_FLOAT_EQ(context->GetDensityIndependentPixelRatio(), window.get_content_scale().x);
        button->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, SkippedPublicationDropsStaleTextThenReacquiresInput) {
        ASSERT_TRUE(text()->Focus());
        character(window.get(), U'旧');
        window.publish_input_frame();
        character(window.get(), U'滞');
        tick();
        EXPECT_TRUE(text()->GetValue().empty());
        EXPECT_FALSE(platform.text_input_active());
        ASSERT_TRUE(text()->Focus());
        character(window.get(), U'新');
        tick();
        EXPECT_EQ(text()->GetValue(), "新");
    }

    TEST_F(RmlPlatformTest, PointerOwnershipLossCancelsHeldPressWithoutClick) {
        auto* button = document->GetElementById("first");
        EventCounter counter;
        button->AddEventListener("click", &counter);
        move_cursor(10, 40);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tick();
        ASSERT_EQ(context->GetFocusElement(), button);
        auto frame = window.get_input_frame();
        frame.pointer_enabled = false;
        platform.update(*context, window, frame, true);
        EXPECT_EQ(counter.clicks, 0);
        EXPECT_EQ(context->GetFocusElement(), button);
        EXPECT_FALSE(context->IsMouseInteracting());
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick();
        EXPECT_EQ(counter.clicks, 0);
        button->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, ButtonEventRestoresHoverAfterCancelWithoutAnotherCursorEvent) {
        auto* button = document->GetElementById("first");
        EventCounter counter;
        button->AddEventListener("click", &counter);
        move_cursor(10, 40);
        tick();
        platform.cancel_input(*context);
        tick(false);
        tick();
        tick();
        ASSERT_FALSE(context->IsMouseInteracting());
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tick();
        ASSERT_EQ(window.get_ui_events().size(), 2u);
        EXPECT_EQ(window.get_ui_events()[0].type, Comet::Window::UiEvent::Type::MouseDown);
        EXPECT_EQ(window.get_ui_events()[1].type, Comet::Window::UiEvent::Type::MouseUp);
        EXPECT_EQ(counter.clicks, 1);
        button->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, GamepadMovesSpatialFocusAndActivatesFocusedControl) {
        auto* first = document->GetElementById("first");
        auto* second = document->GetElementById("second");
        ASSERT_TRUE(first->Focus());
        EventCounter counter;
        second->AddEventListener("click", &counter);
        auto frame = window.publish_input_frame();
        frame.gamepads[0].connected = true;
        frame.gamepads[0].connection_revision = 1;
        platform.update(*context, window, frame, true);
        frame = window.publish_input_frame();
        frame.gamepads[0].connected = true;
        frame.gamepads[0].connection_revision = 1;
        frame.gamepads[0].buttons[static_cast<size_t>(Comet::Input::GamepadButton::DpadDown)] = {
            .down = true, .pressed = true};
        platform.update(*context, window, frame, true);
        EXPECT_EQ(context->GetFocusElement(), second);
        frame = window.publish_input_frame();
        frame.gamepads[0].connected = true;
        frame.gamepads[0].connection_revision = 1;
        frame.gamepads[0].buttons[static_cast<size_t>(Comet::Input::GamepadButton::South)] = {
            .down = true, .pressed = true};
        platform.update(*context, window, frame, true);
        EXPECT_EQ(counter.clicks, 1);
        platform.update(*context, window, frame, true);
        EXPECT_EQ(counter.clicks, 1);
        second->RemoveEventListener("click", &counter);
    }

    TEST_F(RmlPlatformTest, FocusRecoveryAcceptsFreshGamepadConfirmAndIgnoresHeldButton) {
        auto* button = document->GetElementById("first");
        ASSERT_TRUE(button->Focus());
        EventCounter counter;
        button->AddEventListener("click", &counter);
        const ScopeExit detach([&] { button->RemoveEventListener("click", &counter); });
        const auto sample = [&](bool down, bool pressed) {
            auto frame = window.publish_input_frame();
            frame.gamepads[0].connected = true;
            frame.gamepads[0].connection_revision = 1;
            frame.gamepads[0].buttons[static_cast<size_t>(Comet::Input::GamepadButton::South)] = {
                .down = down, .pressed = pressed};
            platform.update(*context, window, frame, true);
        };
        sample(false, false);
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        sample(true, true);
        EXPECT_EQ(counter.clicks, 0);
        ASSERT_TRUE(button->Focus());
        sample(true, false);
        EXPECT_EQ(counter.clicks, 0);
        sample(false, false);
        sample(true, true);
        EXPECT_EQ(counter.clicks, 1);

        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        sample(false, false);
        ASSERT_TRUE(button->Focus());
        sample(true, true);
        EXPECT_EQ(counter.clicks, 2);
    }
}
