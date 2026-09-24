#pragma once

#include "common/export.h"
#include "core/directory_change_signal.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

namespace Comet {
    class COMET_API AssetSourceMonitor final {
    public:
        enum class PollState { NotPolled, Unchanged, Changed, Failed };

        struct PollResult {
            PollState state = PollState::NotPolled;
            std::filesystem::path issue_path;
            std::string message;
        };

        explicit AssetSourceMonitor(std::filesystem::path root,
            std::chrono::milliseconds poll_interval = std::chrono::milliseconds(500));

        [[nodiscard]] PollResult poll();
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

        [[nodiscard]] bool capture_snapshot(
            Snapshot& snapshot, std::filesystem::path& issue_path, std::string& message) const;

        std::filesystem::path m_root;
        DirectoryChangeSignal m_changes;
        Snapshot m_snapshot;
        bool m_initial_poll_attempted = false;
        bool m_has_baseline = false;
        bool m_initial_capture_failed = false;
    };
}
