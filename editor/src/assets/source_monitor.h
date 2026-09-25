#pragma once

#include "common/result.h"
#include "file_recheck_trigger.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Comet {
    class TaskScheduler;
}

namespace CometEditor {
    class AssetSourceMonitor final {
    public:
        using Clock = FileRecheckTrigger::Clock;
        enum class PollState { NotPolled, Unchanged, Changed, Failed };

        struct PollResult {
            PollState state = PollState::NotPolled;
            std::filesystem::path issue_path;
            std::string message;
            std::vector<std::filesystem::path> changed_paths;
            bool requires_full_scan = false;
        };

        explicit AssetSourceMonitor(std::filesystem::path root,
            std::chrono::milliseconds poll_interval = std::chrono::milliseconds(500));

        [[nodiscard]] PollResult poll_async(
            Comet::TaskScheduler& scheduler, Clock::time_point now = Clock::now());
        [[nodiscard]] PollResult poll_now();
        [[nodiscard]] bool uses_native_notifications() const {
            return m_changes.uses_native_notifications();
        }

        [[nodiscard]] bool acknowledge(const std::filesystem::path& relative_path);

    private:
        struct FileState {
            std::filesystem::file_time_type write_time;
            std::uintmax_t size = 0;

            bool operator==(const FileState&) const noexcept = default;
        };

        using Snapshot = std::map<std::filesystem::path, FileState>;

        struct SnapshotIssue {
            std::filesystem::path path;
            std::string message;
        };

        using SnapshotResult = Comet::Result<Snapshot, SnapshotIssue>;

        struct PendingSnapshot {
            std::future<SnapshotResult> completion;
            std::uint64_t generation = 0;
        };

        [[nodiscard]] static SnapshotResult capture_snapshot(const std::filesystem::path& root);
        [[nodiscard]] PollResult accept_snapshot(SnapshotResult result);
        [[nodiscard]] PollResult poll_changed_files(
            const std::vector<std::filesystem::path>& paths);

        std::filesystem::path m_root;
        FileRecheckTrigger m_changes;
        Snapshot m_snapshot;
        bool m_initial_poll_attempted = false;
        bool m_has_baseline = false;
        bool m_initial_capture_failed = false;
        std::optional<PendingSnapshot> m_pending_snapshot;
        std::uint64_t m_snapshot_generation = 0;
        bool m_full_scan_requested = false;
    };
}
