#include "render/shader_reload.h"
#include "core/task_scheduler.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <future>
#include <thread>

namespace CometEditor::Tests {
    class ShaderReloadTest: public testing::Test {
    protected:
        Comet::Tests::TemporaryDirectory directory;
        Comet::TaskScheduler scheduler{1, 1};
        ShaderReload::Clock::time_point now{};
        ShaderReload::Requests requests{
            {"vertex", {.source = directory.path() / "material/unlit_color.vert",
                           .stage = Comet::ShaderStage::Vertex}},
            {"textured", {.source = directory.path() / "material/pbr.frag",
                             .stage = Comet::ShaderStage::Fragment}},
            {"solid", {.source = directory.path() / "material/unlit_color.frag",
                          .stage = Comet::ShaderStage::Fragment}}};

        void SetUp() override {
            std::error_code error;
            std::filesystem::copy(std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders",
                directory.path(), std::filesystem::copy_options::recursive, error);
            ASSERT_FALSE(error) << error.message();
        }
        void write(std::string_view name, std::string_view contents) {
            ASSERT_TRUE(
                Comet::write_text_file_atomic(directory.path() / "material" / name, contents));
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
        EXPECT_EQ(result->stages.size(), requests.size());
        EXPECT_EQ(result->compiled_stages, requests.size());
        for(const auto& [name, stage] : result->stages)
            EXPECT_TRUE(stage.succeeded());
        for(int index = 0; index < 3; ++index) {
            now += std::chrono::seconds(1);
            EXPECT_FALSE(reload.update(now));
            scheduler.wait_idle();
            EXPECT_FALSE(reload.update(now));
        }
    }

    TEST_F(ShaderReloadTest, UsesConfiguredQuietPeriodForExplicitRequests) {
        ShaderReload reload(scheduler, requests, {}, std::chrono::milliseconds(350));
        const auto initial = finish(reload);
        ASSERT_TRUE(initial);

        reload.request(now);
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(349)));
        scheduler.wait_idle();
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(349)));

        now += std::chrono::milliseconds(350);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        const auto next = reload.update(now);
        ASSERT_TRUE(next);
        EXPECT_EQ(next->revision, initial->revision + 1);
        EXPECT_EQ(next->compiled_stages, 0u);
    }

    TEST_F(ShaderReloadTest, SharedVertexIncludeRecompilesEveryMaterialProgram) {
        requests.emplace("texture_vertex",
            Comet::ShaderCompiler::Request{.source = directory.path() / "material/pbr.vert",
                .stage = Comet::ShaderStage::Vertex});
        ShaderReload reload(scheduler, requests);
        const auto original = finish(reload);
        ASSERT_TRUE(original);
        ASSERT_TRUE(original->succeeded) << original->diagnostics;
        const auto path = directory.path() / "common/mesh_vertex.glsl";
        auto source = Comet::read_text_file(path);
        ASSERT_TRUE(source);
        const auto offset = source.value().find("gl_Position =");
        ASSERT_NE(offset, std::string::npos);
        source.value().replace(
            offset, std::string_view("gl_Position =").size(), "gl_Position = 2.0 *");
        ASSERT_TRUE(Comet::write_text_file_atomic(path, source.value()));
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        const auto updated = finish(reload);
        ASSERT_TRUE(updated);
        ASSERT_TRUE(updated->succeeded) << updated->diagnostics;
        EXPECT_EQ(updated->compiled_stages, 2u);
        for(const auto* stage : {"vertex", "texture_vertex"})
            EXPECT_NE(original->stages.at(stage).words, updated->stages.at(stage).words);
        EXPECT_EQ(original->stages.at("solid").words, updated->stages.at("solid").words);
    }

    TEST_F(ShaderReloadTest, FallbackRecheckDoesNotExtendPendingReload) {
        ShaderReload reload(scheduler, requests);
        const auto original = finish(reload);
        ASSERT_TRUE(original);
        ASSERT_TRUE(original->succeeded) << original->diagnostics;

        now += std::chrono::milliseconds(400);
        reload.request(now);
        write("unlit_color.frag", "#version 450\ninvalid source\n");
        now += std::chrono::milliseconds(100);
        EXPECT_FALSE(reload.update(now));

        now += std::chrono::milliseconds(100);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        const auto updated = reload.update(now);
        ASSERT_TRUE(updated);
        EXPECT_EQ(updated->revision, original->revision + 1);
        EXPECT_FALSE(updated->succeeded);
        EXPECT_EQ(updated->compiled_stages, 1u);

        reload.request(now);
        const auto retried = finish(reload);
        ASSERT_TRUE(retried);
        EXPECT_FALSE(retried->succeeded);
        EXPECT_EQ(retried->compiled_stages, 1u);
    }

    TEST_F(ShaderReloadTest, NativeNotificationRecompilesAtomicReplacement) {
        ShaderReload reload(scheduler, requests, directory.path());
        if(!reload.uses_native_notifications())
            GTEST_SKIP() << "Native file notifications are unavailable";
        const auto original = finish(reload);
        ASSERT_TRUE(original);
        ASSERT_TRUE(original->succeeded);

        write("unlit_color.frag", "#version 450\ninvalid source\n");
        std::shared_ptr<const ShaderReload::Compilation> changed;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while(!changed && std::chrono::steady_clock::now() < deadline) {
            now += std::chrono::milliseconds(20);
            auto result = reload.update(now);
            if(result && result->revision > original->revision)
                changed = std::move(result);
            scheduler.wait_idle();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        ASSERT_TRUE(changed);
        EXPECT_FALSE(changed->succeeded);
        EXPECT_GT(changed->revision, original->revision);
    }

    TEST_F(ShaderReloadTest, DeliversAcceptedCompilationAfterSchedulerShutdown) {
        ShaderReload reload(scheduler, requests);
        EXPECT_FALSE(reload.update(now));
        scheduler.shutdown();

        const auto result = reload.update(now);
        ASSERT_TRUE(result);
        EXPECT_TRUE(result->succeeded) << result->diagnostics;
        EXPECT_EQ(result->stages.size(), requests.size());
        EXPECT_FALSE(reload.update(now));

        reload.request(now);
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
    }

    TEST_F(ShaderReloadTest, DiscardsChangedSnapshotBeforePublicationAndDebouncesRetry) {
        ShaderReload reload(scheduler, requests);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        write("unlit_color.frag", "#version 450\nthis is invalid\n");
        EXPECT_FALSE(reload.update(now));
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(199)));
        auto failed = finish(reload);
        ASSERT_TRUE(failed);
        EXPECT_FALSE(failed->succeeded);
        EXPECT_NE(failed->diagnostics.find("unlit_color.frag"), std::string::npos);
        EXPECT_GT(failed->revision, 1u);
    }

    TEST_F(ShaderReloadTest, WatchesMissingIncludesAfterFailureAndRecoversWithoutExplicitRequest) {
        write("unlit_color.frag", "#version 450\n#extension GL_GOOGLE_include_directive : require\n"
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
        EXPECT_EQ(recovered->compiled_stages, 1u);
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
        auto blocker = scheduler.try_submit([&] {
            started.set_value();
            release.get_future().wait();
        });
        ASSERT_TRUE(blocker);
        started.get_future().wait();
        auto queued = scheduler.try_submit([] {});
        EXPECT_TRUE(queued);
        reload.request(now);
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        release.set_value();
        blocker->get();
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

    TEST_F(ShaderReloadTest, InvalidBatchReportsOnceWithoutCompiling) {
        for(const auto& invalid : {ShaderReload::Requests{}, ShaderReload::Requests{{"", {}}}}) {
            ShaderReload reload(scheduler, invalid);
            const auto result = finish(reload);
            ASSERT_TRUE(result);
            EXPECT_FALSE(result->succeeded);
            EXPECT_TRUE(result->stages.empty());
            EXPECT_FALSE(result->diagnostics.empty());
            EXPECT_FALSE(reload.update(now + std::chrono::seconds(1)));
        }
    }

    TEST_F(ShaderReloadTest, RetriesSameCompilationWithBackoffAndStopsAtLimit) {
        ShaderReload reload(scheduler, requests);
        const auto compiled = finish(reload);
        ASSERT_TRUE(compiled);
        ASSERT_TRUE(compiled->succeeded) << compiled->diagnostics;

        for(int attempt = 0; attempt < 3; ++attempt) {
            ASSERT_TRUE(reload.retry_delivery(compiled->revision, now));
            EXPECT_TRUE(
                reload.retry_delivery(compiled->revision, now + std::chrono::milliseconds(500)));
            const auto delay = std::chrono::seconds(1 << attempt);
            EXPECT_FALSE(reload.update(now + delay - std::chrono::milliseconds(1)));
            now += delay;
            const auto retried = reload.update(now);
            EXPECT_EQ(retried, compiled);
            EXPECT_FALSE(reload.update(now));
        }
        EXPECT_FALSE(reload.retry_delivery(compiled->revision, now));
        now += std::chrono::seconds(60);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        EXPECT_FALSE(reload.update(now));

        reload.request(now);
        const auto latest = finish(reload);
        ASSERT_TRUE(latest);
        ASSERT_TRUE(latest->succeeded);
        EXPECT_GT(latest->revision, compiled->revision);
        EXPECT_FALSE(reload.retry_delivery(compiled->revision, now));
        ASSERT_TRUE(reload.retry_delivery(latest->revision, now));
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(999)));
        now += std::chrono::seconds(1);
        EXPECT_EQ(reload.update(now), latest);
        // 消费成功后不再请求交付，即使还有重试额度也不重复发布。
        now += std::chrono::seconds(60);
        EXPECT_FALSE(reload.update(now));
    }

    TEST_F(ShaderReloadTest, RetryRechecksInputsEvenBeforeNextPoll) {
        ShaderReload reload(scheduler, requests);
        const auto compiled = finish(reload);
        ASSERT_TRUE(compiled);
        ASSERT_TRUE(compiled->succeeded);
        reload.retry_delivery(compiled->revision, now);
        EXPECT_FALSE(reload.update(now + std::chrono::milliseconds(600)));
        write("unlit_color.frag", "#version 450\ninvalid\n");
        now += std::chrono::seconds(1);
        // 上次 poll 后输入变化，重试期限先于下次 poll，仍须拒绝旧候选。
        EXPECT_FALSE(reload.update(now));
        const auto latest = finish(reload);
        ASSERT_TRUE(latest);
        EXPECT_FALSE(latest->succeeded);
        EXPECT_GT(latest->revision, compiled->revision);
        reload.retry_delivery(latest->revision, now);
        EXPECT_FALSE(reload.update(now + std::chrono::seconds(1)));
    }

    TEST_F(ShaderReloadTest, NewRequestCancelsRetryAndOldConsumerCannotRescheduleIt) {
        ShaderReload reload(scheduler, requests);
        const auto compiled = finish(reload);
        ASSERT_TRUE(compiled);
        reload.retry_delivery(compiled->revision, now);
        reload.request(now);
        reload.retry_delivery(compiled->revision, now);
        const auto latest = finish(reload);
        ASSERT_TRUE(latest);
        EXPECT_TRUE(latest->succeeded);
        EXPECT_NE(latest, compiled);
        EXPECT_GT(latest->revision, compiled->revision);
        reload.retry_delivery(compiled->revision, now);
        now += std::chrono::seconds(2);
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        EXPECT_FALSE(reload.update(now));
    }
}
