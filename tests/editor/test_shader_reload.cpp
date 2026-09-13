#include "render/shader_reload.h"
#include "core/task_scheduler.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <future>

namespace CometEditor::Tests {
    class ShaderReloadTest: public testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        Comet::TaskScheduler scheduler{1, 1};
        ShaderReload::Clock::time_point now{};
        ShaderReload::Requests requests{{{.source = directory.path() / "material_mesh.vert",
                                             .stage = Comet::ShaderStage::Vertex},
            {.source = directory.path() / "material_textured.frag",
                .stage = Comet::ShaderStage::Fragment},
            {.source = directory.path() / "material_solid.frag",
                .stage = Comet::ShaderStage::Fragment}}};

        void SetUp() override {
            for(const auto& request : requests) {
                const auto input =
                    Comet::read_text_file(std::filesystem::path(PROJECT_ROOT_DIR)
                                          / "engine/shaders/glsl" / request.source.filename());
                ASSERT_TRUE(input) << input.error();
                ASSERT_TRUE(Comet::write_text_file_atomic(request.source, input.value()));
            }
        }
        void write(std::string_view name, std::string_view contents) {
            ASSERT_TRUE(Comet::write_text_file_atomic(directory.path() / name, contents));
        }
        std::shared_ptr<const ShaderReload::Compilation> finish(ShaderReload& reload) {
            now += std::chrono::seconds(1);
            EXPECT_FALSE(reload.update(now));
            scheduler.wait_idle();
            return reload.update(now);
        }
    };

    TEST_F(ShaderReloadTest, CompilesWholeBatchAndDoesNotRepublishUnchangedInputs) {
        ShaderReload reload(scheduler, requests);
        const auto result = finish(reload);
        ASSERT_TRUE(result);
        ASSERT_TRUE(result->succeeded) << result->diagnostics;
        for(const auto& stage : result->stages)
            EXPECT_TRUE(stage.succeeded());
        for(int index = 0; index < 3; ++index) {
            now += std::chrono::seconds(1);
            EXPECT_FALSE(reload.update(now));
            scheduler.wait_idle();
            EXPECT_FALSE(reload.update(now));
        }
    }

    TEST_F(ShaderReloadTest, DiscardsChangedSnapshotBeforePublicationAndDebouncesRetry) {
        ShaderReload reload(scheduler, requests);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        write("material_solid.frag", "#version 450\nthis is invalid\n");
        EXPECT_FALSE(reload.update(now));
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(199)));
        auto failed = finish(reload);
        ASSERT_TRUE(failed);
        EXPECT_FALSE(failed->succeeded);
        EXPECT_NE(failed->diagnostics.find("material_solid.frag"), std::string::npos);
        EXPECT_GT(failed->revision, 1u);
    }

    TEST_F(ShaderReloadTest, WatchesMissingIncludesAfterFailureAndRecoversWithoutExplicitRequest) {
        write("material_solid.frag",
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
            "#include \"missing.glsl\"\nlayout(location=0) out vec4 color;\n"
            "void main(){color=VALUE;}\n");
        ShaderReload reload(scheduler, requests);
        auto failed = finish(reload);
        ASSERT_TRUE(failed);
        EXPECT_FALSE(failed->succeeded);
        write("missing.glsl", "#define VALUE vec4(1)\n");
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        auto recovered = finish(reload);
        ASSERT_TRUE(recovered);
        EXPECT_TRUE(recovered->succeeded) << recovered->diagnostics;
        EXPECT_GT(recovered->revision, failed->revision);
    }

    TEST_F(ShaderReloadTest, NewRequestSupersedesCompletedResultAndQueueFullDoesNotLoseRequest) {
        ShaderReload reload(scheduler, requests);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        reload.request(now);
        EXPECT_FALSE(reload.update(now));
        auto latest = finish(reload);
        ASSERT_TRUE(latest);
        EXPECT_EQ(latest->revision, 2u);

        std::promise<void> started;
        std::promise<void> release;
        auto blocker = scheduler.submit([&] {
            started.set_value();
            release.get_future().wait();
        });
        started.get_future().wait();
        auto queued = scheduler.try_submit([] {});
        EXPECT_TRUE(queued);
        reload.request(now);
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        release.set_value();
        blocker.get();
        scheduler.wait_idle();
        auto retried = finish(reload);
        ASSERT_TRUE(retried);
        EXPECT_EQ(retried->revision, 3u);
        EXPECT_TRUE(retried->succeeded);
    }

    TEST_F(ShaderReloadTest, WorkerDoesNotAccessDestroyedOwner) {
        {
            ShaderReload reload(scheduler, requests);
            EXPECT_FALSE(reload.update(now));
        }
        scheduler.wait_idle();
    }
}
