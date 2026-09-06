#include "core/window.h"

#include <gtest/gtest.h>
#include <memory>
#include <type_traits>
#include <utility>

#ifdef COMET_TEST_EDITOR_UI
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#endif

namespace Comet::Tests {
    static_assert(!std::is_copy_constructible_v<Window>);
    static_assert(!std::is_copy_assignable_v<Window>);

    class WindowTest: public testing::Test {
    protected:
        Config::Window config{.width = 64, .height = 64, .title = "Comet Window Test"};
    };

    TEST_F(WindowTest, InitializesPlatformAndCreatesVulkanWindowDirectly) {
        Window window(config);
        ASSERT_NE(window.get(), nullptr);
        EXPECT_TRUE(glfwVulkanSupported());
        EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_CLIENT_API), GLFW_NO_API);
        EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_RESIZABLE), GLFW_TRUE);
    }

    TEST_F(WindowTest, DestroyingOneWindowDoesNotDestroyAnother) {
        auto first = std::make_unique<Window>(config);
        Window second(config);
        first.reset();
        second.request_close();
        EXPECT_TRUE(second.should_close());
        EXPECT_GT(second.get_framebuffer_size().x, 0u);
        second.poll_events();
    }

    TEST_F(WindowTest, RepeatedWindowsKeepPlatformAliveAndResetCreationHints) {
        for(unsigned index = 0; index < 24; ++index) {
            {
                Window window(config);
                // 这些是全局 creation hints，不应污染下一扇 Comet 窗口。
                glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
                glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
                EXPECT_EQ(glfwGetWindowAttrib(window.get(), GLFW_DECORATED), GLFW_TRUE);
                EXPECT_EQ(
                    glfwGetWindowAttrib(window.get(), GLFW_CLIENT_API), GLFW_NO_API);
            }
            glfwGetError(nullptr);
            EXPECT_GE(glfwGetTime(), 0.0);
            EXPECT_EQ(glfwGetError(nullptr), GLFW_NO_ERROR);
        }
    }

    TEST_F(WindowTest, NativeCallbacksPublishKeysMouseAndScrollAfterPolling) {
        Window window(config);
        window.poll_events();
        const auto key = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), key);
        const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), focus);
        const auto cursor = glfwSetCursorPosCallback(window.get(), nullptr);
        glfwSetCursorPosCallback(window.get(), cursor);
        const auto mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
        glfwSetMouseButtonCallback(window.get(), mouse);
        const auto scroll = glfwSetScrollCallback(window.get(), nullptr);
        glfwSetScrollCallback(window.get(), scroll);
        ASSERT_TRUE(key && focus && cursor && mouse && scroll);
        focus(window.get(), GLFW_FALSE);
        focus(window.get(), GLFW_TRUE);
        using Key = Input::Key;
        const std::array keys{std::pair{GLFW_KEY_A, Key::A},
            std::pair{GLFW_KEY_Z, Key::Z}, std::pair{GLFW_KEY_0, Key::Digit0},
            std::pair{GLFW_KEY_9, Key::Digit9}, std::pair{GLFW_KEY_F1, Key::F1},
            std::pair{GLFW_KEY_F25, Key::F25}, std::pair{GLFW_KEY_KP_0, Key::Keypad0},
            std::pair{GLFW_KEY_KP_9, Key::Keypad9},
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
        scroll(window.get(), 0.5, 2);
        scroll(window.get(), -0.25, 1);
        window.poll_events();
        const auto frame = window.get_input_frame();
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
        window.poll_events();
        for(const auto& [native, translated] : keys) {
            EXPECT_TRUE(window.get_input_frame().key(translated).down);
            EXPECT_FALSE(window.get_input_frame().key(translated).pressed);
            key(window.get(), native, 0, GLFW_RELEASE, 0);
        }
        window.poll_events();
        EXPECT_TRUE(window.get_input_frame().key(Key::Escape).released);
        EXPECT_FALSE(window.should_close());
    }

#ifdef COMET_TEST_EDITOR_UI
    TEST_F(WindowTest, ImGuiChainsInputAndRestoresCallbacksBeforeWindowDestruction) {
        Window window(config);
        window.poll_events();
        const auto original = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), original);
        struct ImGuiBinding {
            bool initialized = false;
            ~ImGuiBinding() {
                if(initialized)
                    ImGui_ImplGlfw_Shutdown();
                ImGui::DestroyContext();
            }
        } binding;
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        binding.initialized = ImGui_ImplGlfw_InitForVulkan(window.get(), true);
        ASSERT_TRUE(binding.initialized);
        EXPECT_EQ(glfwGetWindowUserPointer(window.get()), &window);
        const auto chained = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), chained);
        const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
        glfwSetWindowFocusCallback(window.get(), focus);
        ASSERT_NE(chained, original);
        focus(window.get(), GLFW_TRUE);
        chained(window.get(), GLFW_KEY_W, 0, GLFW_PRESS, 0);
        window.poll_events();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_W));
        EXPECT_TRUE(window.get_input_frame().key(Input::Key::W).pressed);
        ImGui::EndFrame();
        ImGui_ImplGlfw_Shutdown();
        binding.initialized = false;
        const auto restored = glfwSetKeyCallback(window.get(), nullptr);
        glfwSetKeyCallback(window.get(), restored);
        EXPECT_EQ(restored, original);
        restored(window.get(), GLFW_KEY_W, 0, GLFW_RELEASE, 0);
        window.poll_events();
        EXPECT_TRUE(window.get_input_frame().key(Input::Key::W).released);
    }
#endif
}
