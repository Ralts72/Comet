#include "runtime/entry.h"
#include "runtime/application.h"
#include "common/scope_exit.h"
#include "common/file_io.h"
#include "core/project_paths.h"
#include "config/config.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/scene/scene_renderer.h"
#include "graphics/swapchain.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "support/temporary_directory.h"

#ifdef COMET_TEST_EDITOR_IMGUI
#include "ui/imgui_context.h"
#endif

#include <gtest/gtest.h>
#include <vector>

namespace Comet::Tests {
    using RunResult = Result<void, Error>;
    class EntryTest: public ::testing::Test {
    protected:
        inline static std::optional<LaunchOptions> received;
        inline static bool fail = false;

        static RunResult run_host(const LaunchOptions& options) {
            received = options;
            if(fail)
                return RunResult::failure({"Project validation failed"});
            return RunResult::success();
        }

        LaunchOptions options{.config_file = "missing/config.json", .config_profile = "test"};

        void SetUp() override {
            received.reset();
            fail = false;
        }
    };

    TEST_F(EntryTest, HelpDoesNotStartHostOrReadConfiguration) {
        const char* arguments[]{"/Applications/CometEditor", "--help"};
        ::testing::internal::CaptureStdout();
        const int result = launch(2, arguments, options, run_host);
        const auto output = ::testing::internal::GetCapturedStdout();
        EXPECT_EQ(result, 0);
        EXPECT_EQ(output, "Usage: CometEditor [project-directory | project.json]\n");
        EXPECT_FALSE(received);
    }

    TEST_F(EntryTest, InvalidArgumentsDoNotStartHost) {
        for(const std::vector<const char*>& arguments :
            {std::vector<const char*>{"Comet", "--unknown"}, {"Comet", "first", "second"},
                {"Comet", "--help", "project"}}) {
            ::testing::internal::CaptureStderr();
            const int result =
                launch(static_cast<int>(arguments.size()), arguments.data(), options, run_host);
            const auto error = ::testing::internal::GetCapturedStderr();
            EXPECT_EQ(result, 1);
            EXPECT_NE(
                error.find("Expected a project directory or project.json"), std::string::npos);
            EXPECT_FALSE(received);
        }
    }

    TEST_F(EntryTest, DefaultProjectSelectionBelongsToHost) {
        const char* arguments[]{"Comet"};
        EXPECT_EQ(launch(1, arguments, options, run_host), 0);
        ASSERT_TRUE(received);
        EXPECT_FALSE(received->project_path);
        EXPECT_EQ(received->config_file, options.config_file);
        EXPECT_EQ(received->config_profile, options.config_profile);
    }

    TEST_F(EntryTest, ForwardsExplicitProjectWithoutChangingWorkingDirectory) {
        const auto working_directory = std::filesystem::current_path();
        for(const auto* project : {"projects/My Game", "项目/游戏/project.json"}) {
            const char* arguments[]{"CometEditor", project};
            EXPECT_EQ(launch(2, arguments, options, run_host), 0);
            ASSERT_TRUE(received);
            EXPECT_EQ(received->project_path, std::filesystem::path(project));
            EXPECT_EQ(received->config_file, options.config_file);
            EXPECT_EQ(received->config_profile, options.config_profile);
            EXPECT_EQ(std::filesystem::current_path(), working_directory);
        }
    }

    TEST_F(EntryTest, ReportsHostFailureOnceAndReturnsNonzero) {
        const char* arguments[]{"Comet", "project.json"};
        fail = true;
        ::testing::internal::CaptureStderr();
        const int result = launch(2, arguments, options, run_host);
        const auto error = ::testing::internal::GetCapturedStderr();
        EXPECT_EQ(result, 1);
        EXPECT_EQ(error, "Application failed: Project validation failed\n");
        ASSERT_TRUE(received);
        EXPECT_EQ(received->project_path, std::filesystem::path("project.json"));
    }

    TEST(ApplicationCreationTest, FailedEngineCreationSkipsHooksAndAllowsRetry) {
        class App final: public Application {
        public:
            int initializations = 0;
            int shutdowns = 0;
            RunResult on_init() override {
                ++initializations;
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            void on_shutdown() override { ++shutdowns; }
        } app;
        Config config;
        const auto frame_slots = config.render.max_frames_in_flight;
        config.render.max_frames_in_flight = 0;
        const auto failed = app.run(config);
        ASSERT_FALSE(failed);
        EXPECT_EQ(failed.error().message, "Renderer requires at least one frame slot");
        EXPECT_EQ(app.initializations, 0);
        EXPECT_EQ(app.shutdowns, 0);

        config.render.max_frames_in_flight = frame_slots;
        ASSERT_TRUE(app.run(config));
        EXPECT_EQ(app.initializations, 1);
        EXPECT_EQ(app.shutdowns, 1);
    }

    TEST(ApplicationCreationTest, HostTitleOverridesConfigBeforeInitialization) {
        class App final: public Application {
        public:
            using Application::Application;
            std::string expected_title;
            RunResult on_init() override {
                EXPECT_EQ(get_engine().get_window().get_title(), expected_title);
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            void on_shutdown() override {}
        };
        Config config;
        config.window.title = "Configured title";
        for(const std::optional<std::string>& title :
            {std::optional<std::string>{}, std::optional<std::string>{"My Project"},
                std::optional<std::string>{"Comet Editor"}}) {
            App app({.window_title = title});
            app.expected_title = title.value_or(config.window.title);
            ASSERT_TRUE(app.run(config));
        }
    }

    TEST(ApplicationCreationTest,
        DisplaySettingsApplyBeforeInitializationAndHostOutputOverrideWins) {
        class App final: public Application {
        public:
            using Application::Application;
            OutputSettings expected;
            RunResult on_init() override {
                EXPECT_EQ(get_engine().get_renderer().get_output_settings(), expected);
                EXPECT_EQ(get_engine().frame_rate_limit(), 144);
                const auto& swapchain =
                    get_engine().get_renderer().get_render_context().get_swapchain();
                if(expected.mode == OutputMode::Sdr)
                    EXPECT_EQ(
                        swapchain.get_active_generation()->get_config().surface_format.colorSpace,
                        vk::ColorSpaceKHR::eSrgbNonlinear);
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            void on_shutdown() override {}
        };
        Config config;
        config.render.hdr_headroom = 2;
        config.render.hdr_white_level = 0.75f;
        config.window.width = 160;
        config.window.height = 120;
        config.diagnostics.log.enable_file_logging = false;
        const DisplaySettings display{
            160, 120, WindowMode::Windowed, false, {OutputMode::Hdr, 8, 1.25f}, 144};
        for(const auto mode : {std::optional<OutputMode>{}, std::optional{OutputMode::Sdr}}) {
            App app({.output_mode = mode, .display_settings = display});
            app.expected = display.output;
            if(mode)
                app.expected.mode = *mode;
            ASSERT_TRUE(app.run(config));
        }
    }

    TEST(ApplicationCreationTest, SceneOutputUsesConfigUnlessHostExplicitlyOverridesIt) {
        class App final: public Application {
        public:
            using Application::Application;
            bool offscreen = false;
            RunResult on_init() override {
                offscreen = get_engine().get_renderer().get_scene_renderer().is_offscreen();
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            void on_shutdown() override {}
        };
        Config config;
        config.window.width = 160;
        config.window.height = 120;
        config.render.scene_output = Config::Render::SceneOutput::Offscreen;
        config.diagnostics.log.enable_file_logging = false;
        App configured;
        ASSERT_TRUE(configured.run(config));
        EXPECT_TRUE(configured.offscreen);
        App overridden({.scene_output = Config::Render::SceneOutput::Presentation});
        ASSERT_TRUE(overridden.run(config));
        EXPECT_FALSE(overridden.offscreen);
    }

    TEST(ApplicationCreationTest, LogsStayInsideTheSelectedProjectThroughShutdown) {
        TemporaryDirectory directory;
        const ProjectPaths paths(directory.path() / "external-project");
        class App final: public Application {
        public:
            using Application::Application;
            std::filesystem::path log_path;
            RunResult on_init() override {
                log_path = Logger::get_log_file_path();
                LOG_INFO("project startup");
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            void on_shutdown() override { LOG_INFO("project shutdown"); }
        } app({.cache_directory = paths.cache(), .log_directory = paths.logs()});
        Logger::shutdown();
        Config config;
        config.window.width = 160;
        config.window.height = 120;
        config.vulkan.msaa_samples = SampleCount::Count1;
        config.diagnostics.log.level = "info";
        config.diagnostics.log.directory = directory.path() / "wrong-project";
        ASSERT_TRUE(app.run(config));
        EXPECT_EQ(app.log_path.parent_path(), paths.logs());
        EXPECT_FALSE(std::filesystem::exists(config.diagnostics.log.directory));
        const auto contents = read_text_file(app.log_path);
        ASSERT_TRUE(contents) << contents.error();
        EXPECT_NE(contents.value().find("init renderer"), std::string::npos);
        EXPECT_NE(contents.value().find("project startup"), std::string::npos);
        EXPECT_NE(contents.value().find("project shutdown"), std::string::npos);
        EXPECT_TRUE(Logger::get_log_file_path().empty());
    }

    class ApplicationLifecycleTest: public ::testing::TestWithParam<int> {
    protected:
        class TestApplication final: public Application {
        public:
            int fail_at = 0;
            int shutdowns = 0;
            bool engine_alive_during_shutdown = false;
            bool rendering_stopped_during_shutdown = false;
            TemporaryDirectory directory;
#ifdef COMET_TEST_EDITOR_IMGUI
            std::unique_ptr<CometEditor::Ui::ImGuiContext> ui;
#endif
            RunResult on_init() override {
#ifdef COMET_TEST_EDITOR_IMGUI
                auto result = CometEditor::Ui::ImGuiContext::create(get_engine().get_window(),
                    get_engine().get_renderer().get_render_context(),
                    {.ini_path = directory.path() / "imgui.ini",
                        .composition = CometEditor::Ui::ImGuiContext::Composition::Clear});
                if(!result)
                    return RunResult::failure(result.error().as_error());
                ui = std::move(result).value();
#endif
                if(fail_at == 1)
                    return RunResult::failure({"init failure"});
                if(fail_at == 0)
                    get_engine().get_window().request_close();
                return RunResult::success();
            }
            RunResult on_update(const Engine::FrameContext&) override {
                if(fail_at == 4)
                    return RunResult::success();
                if(fail_at == 3)
                    return RunResult::failure(
                        GraphicsError{"update failure", vk::Result::eErrorDeviceLost}.as_error());
                return RunResult::failure({"update failure"});
            }
            RunResult on_frame_ready(const Engine::FrameContext&) override {
#ifdef COMET_TEST_EDITOR_IMGUI
                if(ui->begin_frame()) {
                    const ScopeExit end_ui([this] { ui->end_frame(); });
                    ImGui::TextUnformatted("Frame failure test");
                    return RunResult::failure(
                        GraphicsError{"frame failure", vk::Result::eErrorDeviceLost}.as_error());
                }
#endif
                return RunResult::failure(
                    GraphicsError{"frame failure", vk::Result::eErrorDeviceLost}.as_error());
            }
            void on_shutdown() override {
                ++shutdowns;
                engine_alive_during_shutdown = get_engine().get_window().get() != nullptr;
                rendering_stopped_during_shutdown = !get_engine().get_renderer().prepare_frame();
#ifdef COMET_TEST_EDITOR_IMGUI
                if(fail_at == 4)
                    EXPECT_NE(ImGui::GetDrawData(), nullptr);
#endif
#ifdef COMET_TEST_EDITOR_IMGUI
                ui.reset();
#endif
            }
        };

        void TearDown() override {
            Config::Log log;
            log.enable_file_logging = false;
            log.level = "warn";
            Logger::init(log, false);
        }
    };

    TEST_P(ApplicationLifecycleTest, CleansOnceAndPreservesPrimaryFailure) {
        TestApplication app;
        app.fail_at = GetParam();
        Config config;
        config.window.width = 320;
        config.window.height = 240;
        config.vulkan.msaa_samples = SampleCount::Count1;
        config.diagnostics.log.enable_file_logging = false;
        config.diagnostics.log.level = "warn";
        std::string error;
        const auto result = app.run(config);
        if(!result)
            error = result.error().message;
        std::string expected;
        if(app.fail_at == 1)
            expected = "init failure";
        else if(app.fail_at == 2)
            expected = "update failure";
        else if(app.fail_at == 3) {
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
            EXPECT_NE(error.find("update failure"), std::string::npos);
            expected = error;
        } else if(app.fail_at == 4) {
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().code,
                (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
            expected = "frame failure";
        }
        EXPECT_EQ(error, expected);
        EXPECT_EQ(app.shutdowns, 1);
        EXPECT_TRUE(app.engine_alive_during_shutdown);
        EXPECT_TRUE(app.rendering_stopped_during_shutdown);
#ifdef COMET_TEST_EDITOR_IMGUI
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
#endif
    }

    INSTANTIATE_TEST_SUITE_P(
        NormalAndFailedExit, ApplicationLifecycleTest, ::testing::Values(0, 1, 2, 3, 4));

}
