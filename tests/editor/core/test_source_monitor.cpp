#include "assets/source_monitor.h"
#include "file_recheck_trigger.h"

#include "asset/handle.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace CometEditor::Tests {
    namespace {
        class TemporaryAssetDirectory final {
        public:
            TemporaryAssetDirectory() {
                m_root = std::filesystem::temp_directory_path()
                         / ("comet_asset_source_monitor_test_"
                             + std::to_string(Comet::AssetHandle::generate().value()));
                std::filesystem::create_directories(m_root);
            }

            ~TemporaryAssetDirectory() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

            void write(
                const std::filesystem::path& relative_path, const std::string& contents) const {
                const std::filesystem::path path = m_root / relative_path;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output << contents;
            }

        private:
            std::filesystem::path m_root;
        };
    }

    TEST(FileRecheckTriggerTest, ReportsFallbackOnlyWhenDue) {
        FileRecheckTrigger trigger({}, std::chrono::milliseconds(500));
        const auto now = FileRecheckTrigger::Clock::time_point{};
        EXPECT_EQ(trigger.poll(now), FileRecheckTrigger::Reason::Fallback);
        EXPECT_EQ(
            trigger.poll(now + std::chrono::milliseconds(499)), FileRecheckTrigger::Reason::None);
        EXPECT_EQ(trigger.poll(now + std::chrono::milliseconds(500)),
            FileRecheckTrigger::Reason::Fallback);
    }

    TEST(FileRecheckTriggerTest, ReportsNativeNotification) {
        const TemporaryAssetDirectory directory;
        FileRecheckTrigger trigger(directory.root(), std::chrono::hours(1));
        if(!trigger.uses_native_notifications())
            GTEST_SKIP() << "Native file notifications are unavailable";
        static_cast<void>(trigger.poll());
        directory.write("new.scene", "scene");

        auto reason = FileRecheckTrigger::Reason::None;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while(reason != FileRecheckTrigger::Reason::Notification
              && std::chrono::steady_clock::now() < deadline) {
            reason = trigger.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_EQ(reason, FileRecheckTrigger::Reason::Notification);
    }

    TEST(AssetSourceMonitorTest, ReportsFileChangesOnlyOnce) {
        const TemporaryAssetDirectory directory;
        AssetSourceMonitor monitor(directory.root());
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        directory.write("textures/albedo.png", "first");
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        directory.write("textures/albedo.png", "second version");
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);

        std::filesystem::remove(directory.root() / "textures/albedo.png");
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);
    }

    TEST(AssetSourceMonitorTest, IgnoresAtomicWriteTemporaryFiles) {
        const TemporaryAssetDirectory directory;
        AssetSourceMonitor monitor(directory.root());
        ASSERT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        directory.write(".comet-tmp-material.1", "partial");

        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);
    }

    TEST(AssetSourceMonitorTest, AcknowledgesEditorOwnedWrites) {
        const TemporaryAssetDirectory directory;
        directory.write("materials/default.mat", "first");
        AssetSourceMonitor monitor(directory.root());
        ASSERT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        directory.write("materials/default.mat", "updated contents");
        ASSERT_TRUE(monitor.acknowledge("materials/default.mat"));

        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);
    }

    TEST(AssetSourceMonitorTest, PreservesBaselineAcrossUnavailableRoot) {
        const TemporaryAssetDirectory directory;
        directory.write("mesh.gltf", "mesh");
        AssetSourceMonitor monitor(directory.root());
        ASSERT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        const std::filesystem::path unavailable = directory.root().string() + ".unavailable";
        std::filesystem::rename(directory.root(), unavailable);
        const AssetSourceMonitor::PollResult failed = monitor.poll_now();
        EXPECT_EQ(failed.state, AssetSourceMonitor::PollState::Failed);
        EXPECT_FALSE(failed.message.empty());
        std::filesystem::rename(unavailable, directory.root());

        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);
    }

    TEST(AssetSourceMonitorTest, ReportsRecoveryAfterInitialFailureAsChange) {
        const TemporaryAssetDirectory directory;
        const std::filesystem::path missing = directory.root() / "missing";
        AssetSourceMonitor monitor(missing);
        ASSERT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Failed);

        std::filesystem::create_directories(missing);
        std::ofstream(missing / "new.scene") << "scene";

        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);
    }

    TEST(AssetSourceMonitorTest, ThrottledPollDefersFilesystemWork) {
        const TemporaryAssetDirectory directory;
        AssetSourceMonitor monitor(directory.root(), std::chrono::hours(1));
        ASSERT_EQ(monitor.poll().state, AssetSourceMonitor::PollState::Unchanged);
        directory.write("new.png", "texture");

        if(monitor.uses_native_notifications()) {
            auto state = AssetSourceMonitor::PollState::NotPolled;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while(state != AssetSourceMonitor::PollState::Changed
                  && std::chrono::steady_clock::now() < deadline) {
                state = monitor.poll().state;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            EXPECT_EQ(state, AssetSourceMonitor::PollState::Changed);
            EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);
        } else {
            EXPECT_EQ(monitor.poll().state, AssetSourceMonitor::PollState::NotPolled);
            EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);
        }
    }

    TEST(AssetSourceMonitorTest, NativeNotificationFindsAtomicReplacement) {
        const TemporaryAssetDirectory directory;
        directory.write("material.mat", "old");
        AssetSourceMonitor monitor(directory.root(), std::chrono::hours(1));
        if(!monitor.uses_native_notifications())
            GTEST_SKIP() << "Native file notifications are unavailable";
        ASSERT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);

        directory.write(".comet-tmp-material.1", "replacement contents");
        std::filesystem::rename(
            directory.root() / ".comet-tmp-material.1", directory.root() / "material.mat");

        auto state = AssetSourceMonitor::PollState::NotPolled;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while(state != AssetSourceMonitor::PollState::Changed
              && std::chrono::steady_clock::now() < deadline) {
            state = monitor.poll().state;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        EXPECT_EQ(state, AssetSourceMonitor::PollState::Changed);
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Unchanged);
    }
}
