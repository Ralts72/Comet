#include "runtime/entry.h"

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

    TEST_F(EntryTest, FactoryRejectsUnexpectedArgumentsBeforeConstruction) {
        const char* arguments[]{"Game", "unexpected"};
        ::testing::internal::CaptureStderr();
        const int result = launch(2, arguments, options, {}, create_default);
        const auto error = ::testing::internal::GetCapturedStderr();
        EXPECT_EQ(result, 1);
        EXPECT_NE(
            error.find("does not accept command-line arguments"), std::string::npos);
        EXPECT_EQ(DefaultApplication::constructions, 0);
    }

    TEST_F(EntryTest, ForwardsArgumentsWithoutExecutableAndReportsProjectFailure) {
        const char* arguments[]{"CometEditor", "projects/My Game/project.yaml"};
        ::testing::internal::CaptureStderr();
        const int result =
            launch(2, arguments, options, "[project]", create_from_project);
        const auto error = ::testing::internal::GetCapturedStderr();
        EXPECT_EQ(result, 1);
        EXPECT_EQ(received, std::vector<std::string>{"projects/My Game/project.yaml"});
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
}
