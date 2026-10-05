#include "core/window.h"
#include "config/config.h"

#include <GLFW/glfw3.h>
#include <gtest/gtest.h>
#include <memory>
#include <array>
#include <utility>

#ifdef COMET_TEST_EDITOR_UI
#include "common/scope_exit.h"
#include "support/imgui_context.h"
#include <backends/imgui_impl_glfw.h>
#endif

namespace Comet::Tests {
    class WindowInputTest: public testing::Test {
    protected:
        Window window{Config::Window{.width = 64, .height = 64, .title = "Comet Input Test"}};
        GLFWkeyfun key = nullptr;
        GLFWwindowfocusfun focus = nullptr;

        void SetUp() override {
            window.poll_events();
            key = glfwSetKeyCallback(window.get(), nullptr);
            glfwSetKeyCallback(window.get(), key);
            focus = glfwSetWindowFocusCallback(window.get(), nullptr);
            glfwSetWindowFocusCallback(window.get(), focus);
            ASSERT_TRUE(key && focus);
            focus(window.get(), GLFW_FALSE);
            focus(window.get(), GLFW_TRUE);
        }

        bool wait_for_attribute(const int attribute, const int expected) {
            window.poll_events();
            for(int attempt = 0; attempt < 50; ++attempt) {
                if(glfwGetWindowAttrib(window.get(), attribute) == expected)
                    return true;
                window.wait_events(0.01);
            }
            return glfwGetWindowAttrib(window.get(), attribute) == expected;
        }

        bool focus_native_window() {
            glfwShowWindow(window.get());
            glfwFocusWindow(window.get());
            return wait_for_attribute(GLFW_FOCUSED, GLFW_TRUE);
        }
    };

    TEST_F(WindowInputTest, TranslatesNativeControlsOnlyWhenExplicitlyPublished) {
        const auto cursor = glfwSetCursorPosCallback(window.get(), nullptr);
        glfwSetCursorPosCallback(window.get(), cursor);
        const auto mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
        glfwSetMouseButtonCallback(window.get(), mouse);
        const auto scroll = glfwSetScrollCallback(window.get(), nullptr);
        glfwSetScrollCallback(window.get(), scroll);
        ASSERT_TRUE(cursor && mouse && scroll);
        using Key = Input::Key;
        const std::array keys{std::pair{GLFW_KEY_A, Key::A}, std::pair{GLFW_KEY_Z, Key::Z},
            std::pair{GLFW_KEY_0, Key::Digit0}, std::pair{GLFW_KEY_9, Key::Digit9},
            std::pair{GLFW_KEY_F1, Key::F1}, std::pair{GLFW_KEY_F25, Key::F25},
            std::pair{GLFW_KEY_KP_0, Key::Keypad0}, std::pair{GLFW_KEY_KP_9, Key::Keypad9},
            std::pair{GLFW_KEY_RIGHT_SUPER, Key::RightSuper},
            std::pair{GLFW_KEY_ESCAPE, Key::Escape}};
        for(const auto& [native, translated] : keys) {
            key(window.get(), native, 0, GLFW_PRESS, 0);
            EXPECT_FALSE(window.get_input_frame().key(translated).down);
        }
        key(window.get(), GLFW_KEY_UNKNOWN, 0, GLFW_PRESS, 0);
        cursor(window.get(), 10, 20);
        cursor(window.get(), 13, 24);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        mouse(window.get(), -1, GLFW_PRESS, 0);
        scroll(window.get(), 0.5, 2);
        scroll(window.get(), -0.25, 1);
        const auto frame = window.publish_input_frame();
        for(const auto& [native, translated] : keys) {
            EXPECT_TRUE(frame.key(translated).down);
            EXPECT_TRUE(frame.key(translated).pressed);
            key(window.get(), native, 0, GLFW_REPEAT, 0);
        }
        EXPECT_FALSE(frame.key(Key::Unknown).down);
        EXPECT_EQ(frame.scroll, Math::Vec2(0.25f, 3));
        EXPECT_EQ(frame.cursor_delta, Math::Vec2(3, 4));
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).pressed);
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).released);
        window.publish_input_frame();
        for(const auto& [native, translated] : keys) {
            EXPECT_TRUE(window.get_input_frame().key(translated).down);
            EXPECT_FALSE(window.get_input_frame().key(translated).pressed);
            key(window.get(), native, 0, GLFW_RELEASE, 0);
        }
        window.publish_input_frame();
        EXPECT_TRUE(window.get_input_frame().key(Key::Escape).released);
        EXPECT_FALSE(window.should_close());
    }

    TEST_F(WindowInputTest, PollingAndWaitingDoNotConsumePendingEdges) {
        key(window.get(), GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
        key(window.get(), GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
        window.poll_events();
        window.wait_events(0.001);
        EXPECT_EQ(window.get_input_frame().serial, 0u);
        const auto frame = window.publish_input_frame();
        EXPECT_TRUE(frame.key(Input::Key::Space).pressed);
        EXPECT_TRUE(frame.key(Input::Key::Space).released);
        focus(window.get(), GLFW_FALSE);
        EXPECT_TRUE(window.get_input_frame().focused);
        EXPECT_FALSE(window.publish_input_frame().focused);
    }

    TEST_F(WindowInputTest, CursorLockUsesNativeModesAndOnlyTransitionsResetMotion) {
        if(!focus_native_window())
            GTEST_SKIP() << "The window system did not grant keyboard focus.";
        const auto cursor = glfwSetCursorPosCallback(window.get(), nullptr);
        glfwSetCursorPosCallback(window.get(), cursor);
        const auto mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
        glfwSetMouseButtonCallback(window.get(), mouse);
        const auto scroll = glfwSetScrollCallback(window.get(), nullptr);
        glfwSetScrollCallback(window.get(), scroll);
        ASSERT_TRUE(cursor && mouse && scroll);
        cursor(window.get(), 10, 20);
        cursor(window.get(), 13, 24);
        const auto published = window.publish_input_frame();
        key(window.get(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
        mouse(window.get(), GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, 0);
        scroll(window.get(), 0, 1);
        cursor(window.get(), 30, 40);

        EXPECT_FALSE(window.is_cursor_locked());
        glfwGetError(nullptr);
        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_CURSOR), GLFW_CURSOR_DISABLED);
        EXPECT_EQ(
            glfwGetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION), glfwRawMouseMotionSupported());
        EXPECT_EQ(glfwGetError(nullptr), GLFW_NO_ERROR);
        EXPECT_EQ(window.get_input_frame().serial, published.serial);
        EXPECT_EQ(window.get_input_frame().cursor_delta, published.cursor_delta);
        cursor(window.get(), 1000, 2000);
        const auto locked = window.publish_input_frame();
        EXPECT_EQ(locked.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(locked.interruption, published.interruption);
        EXPECT_TRUE(locked.key(Input::Key::W).pressed);
        EXPECT_TRUE(locked.mouse(Input::MouseButton::Right).pressed);
        EXPECT_EQ(locked.scroll, Math::Vec2(0, 1));

        cursor(window.get(), 1004, 2006);
        window.set_cursor_locked(true);
        const auto held = window.publish_input_frame();
        EXPECT_EQ(held.cursor_delta, Math::Vec2(4, 6));
        EXPECT_TRUE(held.key(Input::Key::W).down);
        EXPECT_TRUE(held.mouse(Input::MouseButton::Right).down);
        EXPECT_EQ(held.interruption, published.interruption);

        cursor(window.get(), 1010, 2020);
        window.set_cursor_locked(false);
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_CURSOR), GLFW_CURSOR_NORMAL);
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION), GLFW_FALSE);
        EXPECT_EQ(glfwGetError(nullptr), GLFW_NO_ERROR);
        cursor(window.get(), 10, 20);
        EXPECT_EQ(window.publish_input_frame().cursor_delta, Math::Vec2(0));
        cursor(window.get(), 12, 23);
        window.set_cursor_locked(false);
        EXPECT_EQ(window.publish_input_frame().cursor_delta, Math::Vec2(2, 3));
    }

    TEST_F(WindowInputTest, FocusLossReleasesCursorAndDoesNotRelockOnRegain) {
        if(!focus_native_window())
            GTEST_SKIP() << "The window system did not grant keyboard focus.";
        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        key(window.get(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
        const auto before = window.publish_input_frame();
        ASSERT_TRUE(before.key(Input::Key::W).down);

        glfwHideWindow(window.get());
        ASSERT_TRUE(wait_for_attribute(GLFW_FOCUSED, GLFW_FALSE));
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION), GLFW_FALSE);
        const auto lost = window.publish_input_frame();
        EXPECT_FALSE(lost.focused);
        EXPECT_TRUE(lost.key(Input::Key::W).released);
        EXPECT_EQ(lost.interruption, before.interruption + 1);
        window.set_cursor_locked(true);
        EXPECT_FALSE(window.is_cursor_locked());

        ASSERT_TRUE(focus_native_window());
        EXPECT_FALSE(window.is_cursor_locked());
        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        const auto close = glfwSetWindowCloseCallback(window.get(), nullptr);
        glfwSetWindowCloseCallback(window.get(), close);
        ASSERT_TRUE(close);
        window.confirm_close_requests(true);
        glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
        close(window.get());
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_FALSE(window.should_close());
        EXPECT_TRUE(window.take_close_request());

        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        window.request_close();
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION), GLFW_FALSE);
        EXPECT_TRUE(window.should_close());
    }

    TEST_F(WindowInputTest, MinimizedWindowRejectsCursorLock) {
        if(!focus_native_window())
            GTEST_SKIP() << "The window system did not grant keyboard focus.";
        glfwIconifyWindow(window.get());
        if(!wait_for_attribute(GLFW_ICONIFIED, GLFW_TRUE))
            GTEST_SKIP() << "The window system does not support iconifying this window.";
        ASSERT_TRUE(window.is_minimized());
        window.set_cursor_locked(true);
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_CURSOR), GLFW_CURSOR_NORMAL);
        EXPECT_EQ(glfwGetInputMode(window.get(), GLFW_RAW_MOUSE_MOTION), GLFW_FALSE);
    }

#ifdef COMET_TEST_EDITOR_UI
    TEST_F(WindowInputTest, ImGuiPreservesCursorLockAndChainsFocusRelease) {
        if(!focus_native_window())
            GTEST_SKIP() << "The window system did not grant keyboard focus.";
        ImGuiTestContext context;
        const bool initialized = ImGui_ImplGlfw_InitForVulkan(window.get(), true);
        const ScopeExit shutdown([&] {
            if(initialized)
                ImGui_ImplGlfw_Shutdown();
        });
        ASSERT_TRUE(initialized);
        window.set_cursor_locked(true);
        ASSERT_TRUE(window.is_cursor_locked());
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        EXPECT_TRUE(window.is_cursor_locked());
        ImGui::EndFrame();

        const auto chained_focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), chained_focus);
        ASSERT_TRUE(chained_focus);
        EXPECT_NE(chained_focus, focus);
        chained_focus(window.get(), GLFW_FALSE);
        EXPECT_FALSE(window.is_cursor_locked());
        EXPECT_FALSE(window.publish_input_frame().focused);
    }

    TEST_F(WindowInputTest, ImGuiChainsAndRestoresNativeInputCallbacks) {
        ImGuiTestContext context;
        bool initialized = ImGui_ImplGlfw_InitForVulkan(window.get(), true);
        const ScopeExit shutdown([&] {
            if(initialized)
                ImGui_ImplGlfw_Shutdown();
        });
        ASSERT_TRUE(initialized);
        EXPECT_EQ(glfwGetWindowUserPointer(window.get()), &window);
        const auto chained_key = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), chained_key);
        const auto chained_focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), chained_focus);
        ASSERT_TRUE(chained_key && chained_focus);
        EXPECT_NE(chained_key, key);
        chained_focus(window.get(), GLFW_TRUE);
        chained_key(window.get(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
        window.publish_input_frame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_W));
        EXPECT_TRUE(window.get_input_frame().key(Input::Key::W).pressed);
        ImGui::EndFrame();
        ImGui_ImplGlfw_Shutdown();
        initialized = false;
        const auto restored = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), restored);
        EXPECT_EQ(restored, key);
        restored(window.get(), GLFW_KEY_W, 0, GLFW_RELEASE, 0);
        EXPECT_TRUE(window.publish_input_frame().key(Input::Key::W).released);
    }
#endif

    TEST(WindowTest, TitleStartsFromConfigurationAndCanBeReplaced) {
        Config::Window config;
        config.width = 64;
        config.height = 64;
        config.title = "Comet Title Test";
        Window window(config);
        EXPECT_EQ(window.get_title(), config.title);

        const auto updated = config.title + " | 120 FPS";
        window.set_title(updated);
        EXPECT_EQ(window.get_title(), updated);
        EXPECT_STREQ(glfwGetWindowTitle(window.get()), updated.c_str());

        window.set_title(config.title);
        EXPECT_EQ(window.get_title(), config.title);
    }

    TEST(WindowTest, CloseConfirmationConsumesNativeRequestsWithoutStoppingTheLoop) {
        Config::Window config;
        config.width = 64;
        config.height = 64;
        Window window(config);
        const auto callback = glfwSetWindowCloseCallback(window.get(), nullptr);
        ASSERT_NE(callback, nullptr);
        glfwSetWindowCloseCallback(window.get(), callback);
        window.confirm_close_requests(true);
        glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
        callback(window.get());
        EXPECT_FALSE(window.should_close());
        EXPECT_TRUE(window.take_close_request());
        EXPECT_FALSE(window.take_close_request());
        window.request_close();
        EXPECT_TRUE(window.should_close());
        glfwSetWindowShouldClose(window.get(), GLFW_FALSE);
        window.confirm_close_requests(false);
        glfwSetWindowShouldClose(window.get(), GLFW_TRUE);
        callback(window.get());
        EXPECT_TRUE(window.should_close());
        EXPECT_FALSE(window.take_close_request());
    }

    TEST(WindowTest, WindowsOwnBackendLifetimeAndCloseIndependently) {
        Config::Window config;
        config.width = 64;
        config.height = 64;
        config.title = "Comet Window Lifetime Test";
        {
            auto first = std::make_unique<Window>(config);
            Window second(config);
            EXPECT_FALSE(first->should_close());
            first->request_close();
            EXPECT_TRUE(first->should_close());
            EXPECT_FALSE(second.should_close());
            first.reset();

            second.poll_events();
            EXPECT_GT(second.get_framebuffer_size().x, 0U);
            EXPECT_EQ(second.is_minimized(),
                glfwGetWindowAttrib(second.get(), GLFW_ICONIFIED) == GLFW_TRUE);
            second.request_close();
            EXPECT_TRUE(second.should_close());
        }

        Window reopened(config);
        EXPECT_FALSE(reopened.should_close());
        EXPECT_GT(reopened.get_framebuffer_size().x, 0U);
    }

    TEST(WindowTest, FileDropCopiesCallbackPathsAndConsumesEachEventOnce) {
        Config::Window config;
        config.width = 64;
        config.height = 64;
        config.title = "Comet File Drop Test";
        Window window(config);
        const auto callback = glfwSetDropCallback(window.get(), nullptr);
        ASSERT_NE(callback, nullptr);
        glfwSetDropCallback(window.get(), callback);
        char first[] = "/external/model.gltf";
        const char* paths[]{first, "/external/纹理.png"};
        double x, y;
        glfwGetCursorPos(window.get(), &x, &y);
        callback(window.get(), 2, paths);
        first[1] = 'X';
        callback(window.get(), 1, paths + 1);
        const auto events = window.take_file_drops();
        ASSERT_EQ(events.size(), 2);
        ASSERT_EQ(events[0].paths.size(), 2);
        EXPECT_EQ(events[0].paths[0], "/external/model.gltf");
        EXPECT_EQ(events[0].position, Math::Vec2(static_cast<float>(x), static_cast<float>(y)));
        EXPECT_EQ(events[1].paths[0], std::filesystem::path(u8"/external/纹理.png"));
        EXPECT_TRUE(window.take_file_drops().empty());
    }
}
