#include "asset/source_monitor.h"

#include "asset/handle.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace Comet::Tests {
    namespace {
        class TemporaryAssetDirectory final {
        public:
            TemporaryAssetDirectory() {
                m_root = std::filesystem::temp_directory_path()
                         / ("comet_asset_source_monitor_test_"
                             + std::to_string(AssetHandle::generate().value()));
                std::filesystem::create_directories(m_root);
            }

            ~TemporaryAssetDirectory() {
                std::error_code error;
                std::filesystem::remove_all(m_root, error);
            }

            [[nodiscard]] const std::filesystem::path& root() const { return m_root; }

            void write(const std::filesystem::path& relative_path,
                const std::string& contents) const {
                const std::filesystem::path path = m_root / relative_path;
                std::filesystem::create_directories(path.parent_path());
                std::ofstream output(path, std::ios::binary);
                output << contents;
            }

        private:
            std::filesystem::path m_root;
        };
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

        const std::filesystem::path unavailable =
            directory.root().string() + ".unavailable";
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

        EXPECT_EQ(monitor.poll().state, AssetSourceMonitor::PollState::NotPolled);
        EXPECT_EQ(monitor.poll_now().state, AssetSourceMonitor::PollState::Changed);
    }
}
