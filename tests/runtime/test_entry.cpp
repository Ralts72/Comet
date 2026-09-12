#include "runtime/entry.h"
#include "config/config.h"
#include "render/renderer.h"
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
    class EntryTest: public ::testing::Test {
    protected:
        class DefaultApplication final: public Application {
        public:
            inline static int constructions = 0;
            inline static int destructions = 0;

            DefaultApplication() { ++constructions; }
            ~DefaultApplication() override { ++destructions; }
            void on_init() override {
                ADD_FAILURE() << "Graphics must not be initialized";
            }
            void on_update(UpdateContext) override {}
            void on_shutdown() override {}
        };

        inline static std::vector<std::string> received;

        static std::unique_ptr<Application> create_default(
            ApplicationArguments arguments) {
            if(!arguments.empty())
                throw std::invalid_argument(
                    "This application does not accept command-line arguments");
            return std::make_unique<DefaultApplication>();
        }

        static std::unique_ptr<Application> create_from_project(
            ApplicationArguments arguments) {
            received.assign(arguments.begin(), arguments.end());
            throw std::runtime_error("Project validation failed");
        }

        LaunchOptions options{
            .config_directory =
                std::filesystem::temp_directory_path()
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
        const int result =
            launch(2, arguments, options, "[project]", create_from_project);
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
    class ApplicationLifecycleTest
        : public ::testing::TestWithParam<std::pair<int, bool>> {
    protected:
        class TestApplication final: public Application {
        public:
            int fail_at = 0;
            bool fail_shutdown = false;
            int shutdowns = 0;
            bool engine_alive_during_shutdown = false;
            TemporaryDirectory directory;
#ifdef COMET_TEST_EDITOR_UI
            std::unique_ptr<CometEditor::ImGuiContext> ui;
#endif
            void on_init() override {
#ifdef COMET_TEST_EDITOR_UI
                ui =
                    std::make_unique<CometEditor::ImGuiContext>(get_engine().get_window(),
                        get_engine().get_renderer().get_render_context(),
                        directory.path() / "imgui.ini");
#endif
                if(fail_at == 1)
                    throw std::runtime_error("init failure");
                if(fail_at == 0)
                    get_engine().get_window().request_close();
            }
            void on_update(UpdateContext) override {
                throw std::runtime_error("update failure");
            }
            void on_shutdown() override {
                ++shutdowns;
                engine_alive_during_shutdown = get_engine().get_window().get() != nullptr;
                if(fail_shutdown)
                    throw std::runtime_error("shutdown failure");
#ifdef COMET_TEST_EDITOR_UI
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
        try {
            app.run(config);
        } catch(const std::runtime_error& failure) {
            error = failure.what();
        }
        std::string expected;
        if(app.fail_at == 1)
            expected = "init failure";
        else if(app.fail_at == 2)
            expected = "update failure";
        else if(app.fail_shutdown)
            expected = "shutdown failure";
        EXPECT_EQ(error, expected);
        EXPECT_EQ(app.shutdowns, 1);
        EXPECT_TRUE(app.engine_alive_during_shutdown);
#ifdef COMET_TEST_EDITOR_UI
        if(app.fail_shutdown)
            EXPECT_NE(ImGui::GetCurrentContext(), nullptr);
        else
            EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
#endif
        if(app.fail_shutdown) {
            EXPECT_THROW(app.run(config), std::logic_error);
            EXPECT_EQ(app.shutdowns, 1);
        }
        owner.reset();
#ifdef COMET_TEST_EDITOR_UI
        EXPECT_EQ(ImGui::GetCurrentContext(), nullptr);
#endif
    }

    INSTANTIATE_TEST_SUITE_P(NormalAndExceptionalExit, ApplicationLifecycleTest,
        ::testing::Values(std::pair{0, false}, std::pair{0, true}, std::pair{1, false},
            std::pair{1, true}, std::pair{2, false}, std::pair{2, true}));

}
