#include "asset/source_monitor.h"

#include <system_error>
#include <utility>

namespace Comet {
    namespace {
        bool is_temporary_asset_write(
            const std::filesystem::path& path) {
            return path.filename().string().starts_with(".comet-tmp-");
        }

        bool is_valid_relative_path(
            const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute()) {
                return false;
            }
            const std::filesystem::path normalized = path.lexically_normal();
            return normalized != "." && normalized.begin() != normalized.end()
                && *normalized.begin() != "..";
        }
    }

    AssetSourceMonitor::AssetSourceMonitor(
        std::filesystem::path root,
        const std::chrono::milliseconds poll_interval)
        : m_root(std::move(root).lexically_normal()),
          m_poll_interval(poll_interval) {
        if(m_poll_interval < std::chrono::milliseconds::zero()) {
            m_poll_interval = std::chrono::milliseconds::zero();
        }
    }

    AssetSourceMonitor::PollResult AssetSourceMonitor::poll() {
        const auto now = std::chrono::steady_clock::now();
        if(now < m_next_poll) {
            return {};
        }
        m_next_poll = now + m_poll_interval;
        return poll_now();
    }

    AssetSourceMonitor::PollResult AssetSourceMonitor::poll_now() {
        Snapshot snapshot;
        PollResult result;
        if(!capture_snapshot(
               snapshot, result.issue_path, result.message)) {
            result.state = PollState::Failed;
            if(!m_has_baseline) {
                m_initial_capture_failed = true;
            }
            return result;
        }

        const bool changed = m_has_baseline
            ? snapshot != m_snapshot
            : m_initial_capture_failed;
        m_snapshot = std::move(snapshot);
        m_has_baseline = true;
        m_initial_capture_failed = false;
        result.state = changed ? PollState::Changed : PollState::Unchanged;
        return result;
    }

    bool AssetSourceMonitor::acknowledge(
        const std::filesystem::path& relative_path) {
        if(!m_has_baseline || !is_valid_relative_path(relative_path)) {
            return false;
        }

        const std::filesystem::path normalized =
            relative_path.lexically_normal();
        const std::filesystem::path absolute_path = m_root / normalized;
        std::error_code error;
        const bool exists = std::filesystem::exists(absolute_path, error);
        if(error) {
            return false;
        }
        if(!exists) {
            m_snapshot.erase(normalized);
            return true;
        }

        if(!std::filesystem::is_regular_file(absolute_path, error) || error) {
            return false;
        }
        const auto write_time =
            std::filesystem::last_write_time(absolute_path, error);
        if(error) {
            return false;
        }
        const std::uintmax_t size =
            std::filesystem::file_size(absolute_path, error);
        if(error) {
            return false;
        }

        m_snapshot[normalized] = FileState{
            .write_time = write_time,
            .size = size
        };
        return true;
    }

    bool AssetSourceMonitor::capture_snapshot(
        Snapshot& snapshot,
        std::filesystem::path& issue_path,
        std::string& message) const {
        std::error_code error;
        const bool exists = std::filesystem::exists(m_root, error);
        if(error) {
            issue_path = m_root;
            message = "failed to access assets directory: "
                + error.message();
            return false;
        }
        if(!exists) {
            issue_path = m_root;
            message = "assets directory does not exist";
            return false;
        }
        if(!std::filesystem::is_directory(m_root, error)) {
            issue_path = m_root;
            message = error
                ? "failed to access assets directory: " + error.message()
                : "assets path is not a directory";
            return false;
        }

        std::filesystem::recursive_directory_iterator iterator(
            m_root,
            std::filesystem::directory_options::none,
            error);
        const std::filesystem::recursive_directory_iterator end;
        if(error) {
            issue_path = m_root;
            message = "failed to scan assets directory: " + error.message();
            return false;
        }

        while(iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            const bool regular_file = entry.is_regular_file(error);
            if(error) {
                issue_path = entry.path();
                message = "failed to inspect asset source: "
                    + error.message();
                return false;
            }

            if(regular_file && !is_temporary_asset_write(entry.path())) {
                const auto write_time = entry.last_write_time(error);
                if(error) {
                    issue_path = entry.path();
                    message = "failed to read asset source write time: "
                        + error.message();
                    return false;
                }
                const std::uintmax_t size = entry.file_size(error);
                if(error) {
                    issue_path = entry.path();
                    message = "failed to read asset source size: "
                        + error.message();
                    return false;
                }

                snapshot.emplace(
                    entry.path().lexically_relative(m_root).lexically_normal(),
                    FileState{
                        .write_time = write_time,
                        .size = size
                    });
            }

            iterator.increment(error);
            if(error) {
                issue_path = entry.path();
                message = "failed while scanning assets directory: "
                    + error.message();
                return false;
            }
        }
        return true;
    }
}
