#include "runtime/entry.h"
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

#ifdef COMET_TEST_EDITOR_UI
#include "ui/imgui_context.h"
#endif

#include <gtest/gtest.h>
#include <random>
#include <stdexcept>
#include <vector>

namespace Comet::Tests {
    using RunResult = Result<void, Error>;
    class EntryTest: public ::testing::Test {
    protected:
        class DefaultApplication final: public Application {
        public:
            inline static int constructions = 0;
            inline static int destructions = 0;

            DefaultApplication() { ++constructions; }
            ~DefaultApplication() override { ++destructions; }
            RunResult on_init() override {
                ADD_FAILURE() << "Graphics must not be initialized";
                return RunResult::failure({"Graphics must not be initialized"});
            }
            RunResult on_shutdown() override { return RunResult::success(); }
        };

        inline static std::vector<std::string> received;

        static Result<std::unique_ptr<Application>> create_default(ApplicationArguments arguments) {
            if(!arguments.empty())
                return Result<std::unique_ptr<Application>>::failure(
                    "This application does not accept command-line arguments");
            return Result<std::unique_ptr<Application>>::success(
                std::make_unique<DefaultApplication>());
        }

        static Result<std::unique_ptr<Application>> create_from_project(
            ApplicationArguments arguments) {
            received.assign(arguments.begin(), arguments.end());
            return Result<std::unique_ptr<Application>>::failure("Project validation failed");
        }

        LaunchOptions options{
            .config_directory = std::filesystem::temp_directory_path()
                                / ("comet_entry_missing_" + std::to_string(std::random_device{}())),
            .config_profile = "test"};

        void SetUp() override {
            DefaultApplication::constructions = 0;
            DefaultApplication::destructions = 0;
            received.clear();
            ASSERT_FALSE(std::filesystem::exists(options.config_directory));
        }
    };

    TEST_F(EntryTest, HelpDoesNotConstructApplicationOrReadConfiguration) {
        const char* arguments[]{"CometEditor", "--help"};
        ::testing::internal::CaptureStdout();
        const int result = launch(2, arguments, options, "[project]", create_default);
        const auto output = ::testing::internal::GetCapturedStdout();
        EXPECT_EQ(result, 0);
        EXPECT_EQ(output, "Usage: CometEditor [project]\n");
        EXPECT_EQ(DefaultApplication::constructions, 0);
    }

    TEST_F(EntryTest, ForwardsArgumentsWithoutExecutableAndReportsProjectFailure) {
        const char* arguments[]{"CometEditor", "projects/My Game/project.json"};
        ::testing::internal::CaptureStderr();
        const int result = launch(2, arguments, options, "[project]", create_from_project);
        const auto error = ::testing::internal::GetCapturedStderr();
        EXPECT_EQ(result, 1);
        EXPECT_EQ(received, std::vector<std::string>{"projects/My Game/project.json"});
        EXPECT_NE(error.find("Project validation failed"), std::string::npos);
    }

    TEST_F(EntryTest, ConfigurationFailureDestroysConstructedApplication) {
        const char* arguments[]{"Game"};
        ::testing::internal::CaptureStderr();
        const int result = launch(1, arguments, options, {}, create_default);
        const auto error = ::testing::internal::GetCapturedStderr();
        EXPECT_EQ(result, 1);
        EXPECT_NE(error.find("common.yaml"), std::string::npos);
        EXPECT_EQ(DefaultApplication::constructions, 1);
        EXPECT_EQ(DefaultApplication::destructions, 1);
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
            RunResult on_shutdown() override {
                ++shutdowns;
                return RunResult::success();
            }
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
            RunResult on_shutdown() override { return RunResult::success(); }
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

    TEST(ApplicationCreationTest, OutputOverrideWinsBeforeGraphicsInitialization) {
        class App final: public Application {
        public:
            using Application::Application;
            RunResult on_init() override {
                const auto& swapchain =
                    get_engine().get_renderer().get_render_context().get_swapchain();
                EXPECT_EQ(swapchain.get_active_generation()->get_config().surface_format.colorSpace,
                    vk::ColorSpaceKHR::eSrgbNonlinear);
                get_engine().get_window().request_close();
                return RunResult::success();
            }
            RunResult on_shutdown() override { return RunResult::success(); }
        } app({.output_mode = OutputMode::Sdr});
        Config config;
        config.render.output_mode = OutputMode::Hdr;
        config.window.width = 160;
        config.window.height = 120;
        config.diagnostics.log.enable_file_logging = false;
        ASSERT_TRUE(app.run(config));
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
            RunResult on_shutdown() override { return RunResult::success(); }
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
            RunResult on_shutdown() override {
                LOG_INFO("project shutdown");
                return RunResult::success();
            }
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

    class ApplicationLifecycleTest: public ::testing::TestWithParam<std::pair<int, bool>> {
    protected:
        class TestApplication final: public Application {
        public:
            int fail_at = 0;
            bool fail_shutdown = false;
            int shutdowns = 0;
            bool engine_alive_during_shutdown = false;
            bool rendering_stopped_during_shutdown = false;
            TemporaryDirectory directory;
#ifdef COMET_TEST_EDITOR_UI
            std::unique_ptr<CometEditor::ImGuiContext> ui;
#endif
            RunResult on_init() override {
#ifdef COMET_TEST_EDITOR_UI
                auto result = CometEditor::ImGuiContext::create(get_engine().get_window(),
                    get_engine().get_renderer().get_render_context(),
                    directory.path() / "imgui.ini");
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
            RunResult on_update(Engine::FrameContext&) override {
                if(fail_at == 4)
                    return RunResult::success();
                if(fail_at == 3)
                    return RunResult::failure(
                        GraphicsError{"update failure", vk::Result::eErrorDeviceLost}.as_error());
                return RunResult::failure({"update failure"});
            }
            RunResult on_frame_ready(Engine::FrameContext&) override {
#ifdef COMET_TEST_EDITOR_UI
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
            RunResult on_shutdown() override {
                ++shutdowns;
                engine_alive_during_shutdown = get_engine().get_window().get() != nullptr;
                rendering_stopped_during_shutdown = !get_engine().get_renderer().prepare_frame();
#ifdef COMET_TEST_EDITOR_UI
                if(fail_at == 4)
                    EXPECT_NE(ImGui::GetDrawData(), nullptr);
#endif
                if(fail_shutdown)
                    return RunResult::failure({"shutdown failure"});
#ifdef COMET_TEST_EDITOR_UI
                ui.reset();
#endif
                return RunResult::success();
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
        auto owner = std::make_unique<TestApplication>();
        auto& app = *owner;
        app.fail_at = GetParam().first;
        app.fail_shutdown = GetParam().second;
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
        } else if(app.fail_shutdown)
            expected = "shutdown failure";
        EXPECT_EQ(error, expected);
        EXPECT_EQ(app.shutdowns, 1);
        EXPECT_TRUE(app.engine_alive_during_shutdown);
        EXPECT_TRUE(app.rendering_stopped_during_shutdown);
#ifdef COMET_TEST_EDITOR_UI
        if(app.fail_shutdown)
            EXPECT_NE(ImGui::GetCurrentContext(), nullptr);
        else
            EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
#endif
        if(app.fail_shutdown) {
            const auto restarted = app.run(config);
            ASSERT_FALSE(restarted);
            EXPECT_EQ(restarted.error().message, "Application is already started");
            EXPECT_EQ(app.shutdowns, 1);
        }
        owner.reset();
#ifdef COMET_TEST_EDITOR_UI
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
#endif
    }

    INSTANTIATE_TEST_SUITE_P(NormalAndFailedExit, ApplicationLifecycleTest,
        ::testing::Values(std::pair{0, false}, std::pair{0, true}, std::pair{1, false},
            std::pair{1, true}, std::pair{2, false}, std::pair{2, true}, std::pair{3, false},
            std::pair{3, true}, std::pair{4, false}, std::pair{4, true}));

}
