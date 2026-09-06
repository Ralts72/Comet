#include "shader_reload.h"
#include "asset/handle.h"
#include "common/file_io.h"
#include "core/task_scheduler_test_utils.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class ShaderReloadTest: public testing::Test {
    protected:
        using Reload = CometEditor::ShaderReload;
        std::filesystem::path root =
            std::filesystem::temp_directory_path()
            / ("comet_shader_reload_" + std::to_string(AssetHandle::generate().value()));
        TaskScheduler scheduler{1, 1};
        Reload::Clock::time_point now = Reload::Clock::now();
        Reload::Requests requests{{"shader", {.source = root / "source.vert"}}};
        void SetUp() override {
            write("#version 450\nvoid main(){gl_Position=vec4(1);}");
        }
        void TearDown() override {
            scheduler.wait_idle();
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
        void write(const std::string& text) {
            write_text_file_atomic(root / "source.vert", text);
        }
        Reload make_reload(std::chrono::milliseconds debounce = {}) {
            return Reload(scheduler, requests,
                {.poll_interval = std::chrono::milliseconds(0), .debounce = debounce});
        }
        std::optional<ShaderManager::Bytecodes> complete(Reload& reload) {
            scheduler.wait_idle();
            return reload.update(now);
        }
    };

    TEST_F(ShaderReloadTest, CompilesInitialSnapshotAndDoesNotResubmitIdleSources) {
        auto reload = make_reload();
        EXPECT_FALSE(reload.update(now));
        const auto candidate = complete(reload);
        ASSERT_TRUE(candidate);
        ASSERT_EQ(candidate->size(), 1u);
        EXPECT_EQ(candidate->at("shader").words,
            ShaderCompiler::compile(requests.at("shader")).words);
        for(int index = 0; index < 10; ++index)
            EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().submitted, 1u);
        EXPECT_FALSE(reload.is_busy());
    }

    TEST_F(ShaderReloadTest, AcceptedSnapshotEstablishesTheNextPollingBaseline) {
        BlockedWorker blocker(scheduler);
        Reload reload(scheduler, requests,
            {.poll_interval = std::chrono::seconds(1), .debounce = {}});
        EXPECT_FALSE(reload.update(now));
        write("#version 450\nvoid main(){gl_Position=vec4(2);} // read by queued worker");
        blocker.release();
        scheduler.wait_idle();
        now += std::chrono::milliseconds(1);
        ASSERT_TRUE(reload.update(now));
        now += std::chrono::seconds(1);
        EXPECT_FALSE(reload.update(now));
        EXPECT_FALSE(reload.is_busy());
        EXPECT_EQ(reload.get_statistics().submitted, 1u);
    }

    TEST_F(ShaderReloadTest, SeparateCohortsDoNotBlockEachOthersRecovery) {
        auto valid = make_reload();
        write_text_file_atomic(root / "broken.vert", "not a shader");
        Reload invalid(scheduler, {{"broken", {.source = root / "broken.vert"}}},
            {.poll_interval = {}, .debounce = {}});
        EXPECT_FALSE(invalid.update(now));
        EXPECT_FALSE(complete(invalid));
        EXPECT_EQ(invalid.get_statistics().failed, 1u);
        EXPECT_FALSE(valid.update(now));
        ASSERT_TRUE(complete(valid));
        EXPECT_FALSE(valid.is_busy());
        write_text_file_atomic(
            root / "broken.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        EXPECT_FALSE(invalid.update(now));
        ASSERT_TRUE(complete(invalid));
        EXPECT_FALSE(valid.update(now));
        EXPECT_EQ(valid.get_statistics().submitted, 1u);
    }

    TEST_F(ShaderReloadTest, CoalescesChangesAndDiscardsOldRequestEvenIfItReadNewBytes) {
        BlockedWorker blocker(scheduler);
        auto reload = make_reload(std::chrono::milliseconds(10));
        EXPECT_FALSE(reload.update(now));
        write("#version 450\nvoid main(){gl_Position=vec4(2);} // version two");
        now += std::chrono::milliseconds(1);
        EXPECT_FALSE(reload.update(now));
        write("#version 450\nvoid main(){gl_Position=vec4(3);} // latest version three");
        now += std::chrono::milliseconds(1);
        EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().submitted, 1u);
        EXPECT_EQ(reload.get_statistics().revision, 3u);
        blocker.release();
        scheduler.wait_idle();
        EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().discarded, 1u);
        now += std::chrono::milliseconds(10);
        EXPECT_FALSE(reload.update(now));
        const auto latest = complete(reload);
        ASSERT_TRUE(latest);
        EXPECT_EQ(latest->at("shader").words,
            ShaderCompiler::compile(requests.at("shader")).words);
        EXPECT_EQ(reload.get_statistics().submitted, 2u);
    }

    TEST_F(ShaderReloadTest, DefersFullQueueWithoutBlockingOrRunningCompilerOnOwner) {
        BlockedWorker blocker(scheduler);
        auto queued = scheduler.submit([] {});
        auto reload = make_reload();
        EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().submitted, 0u);
        EXPECT_EQ(reload.get_statistics().backpressure, 1u);
        EXPECT_TRUE(reload.is_busy());
        blocker.release();
        queued.get();
        EXPECT_FALSE(reload.update(now));
        EXPECT_TRUE(complete(reload));
        EXPECT_EQ(reload.get_statistics().submitted, 1u);
    }

    TEST_F(
        ShaderReloadTest, MissingIncludeRecoversAndFailedCohortNeverPartiallyCompletes) {
        write(
            "#version 450\n#extension GL_GOOGLE_include_directive : require\n#include \"value.glsl\"\nvoid main(){gl_Position=vec4(VALUE);}");
        write_text_file_atomic(
            root / "other.vert", "#version 450\nvoid main(){gl_Position=vec4(1);}");
        requests.emplace("other", ShaderCompiler::Request{.source = root / "other.vert"});
        auto reload = make_reload();
        EXPECT_FALSE(reload.update(now));
        EXPECT_FALSE(complete(reload));
        EXPECT_EQ(reload.get_statistics().failed, 1u);
        EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().submitted, 1u);
        write_text_file_atomic(root / "value.glsl", "#define VALUE 2.0\n");
        EXPECT_FALSE(reload.update(now));
        const auto repaired = complete(reload);
        ASSERT_TRUE(repaired);
        EXPECT_EQ(repaired->size(), 2u);
        write_text_file_atomic(root / "value.glsl", "#define VALUE 30.0\n");
        EXPECT_FALSE(reload.update(now));
        const auto changed = complete(reload);
        ASSERT_TRUE(changed);
        EXPECT_NE(changed->at("shader").words, repaired->at("shader").words);
    }

    TEST_F(ShaderReloadTest, RechecksContentsBeforeHandingCompletedResultToOwner) {
        auto reload = make_reload();
        EXPECT_FALSE(reload.update(now));
        scheduler.wait_idle();
        const auto timestamp = std::filesystem::last_write_time(root / "source.vert");
        write("#version 450\nvoid main(){gl_Position=vec4(2);}");
        std::filesystem::last_write_time(root / "source.vert", timestamp);
        EXPECT_FALSE(reload.update(now));
        EXPECT_EQ(reload.get_statistics().discarded, 1u);
        const auto current = complete(reload);
        ASSERT_TRUE(current);
        EXPECT_EQ(current->at("shader").words,
            ShaderCompiler::compile(requests.at("shader")).words);
    }

    TEST_F(ShaderReloadTest, DestroyedOwnerDoesNotLeaveWorkerReferencesToIt) {
        BlockedWorker blocker(scheduler);
        {
            auto reload = make_reload();
            EXPECT_FALSE(reload.update(now));
            EXPECT_TRUE(reload.is_busy());
        }
        blocker.release();
        scheduler.wait_idle();
        auto next = make_reload();
        EXPECT_FALSE(next.update(now));
        EXPECT_TRUE(complete(next));
    }
}
