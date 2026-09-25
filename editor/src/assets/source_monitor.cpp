#include "assets/source_monitor.h"
#include "core/task_scheduler.h"
#include "diagnostics/profiler.h"

#include <chrono>
#include <cstddef>
#include <system_error>
#include <utility>

namespace CometEditor {
    namespace {
        constexpr std::size_t MAX_PENDING_PATHS = 1024;

        bool is_ignored_asset_source(const std::filesystem::path& path) {
            const auto filename = path.filename();
            return filename == ".DS_Store" || filename.string().starts_with(".comet-tmp-");
        }

        bool is_valid_relative_path(const std::filesystem::path& path) {
            if(path.empty() || path.is_absolute()) {
                return false;
            }
            const std::filesystem::path normalized = path.lexically_normal();
            return normalized != "." && normalized.begin() != normalized.end()
                   && *normalized.begin() != "..";
        }
    }

    AssetSourceMonitor::AssetSourceMonitor(
        std::filesystem::path root, const std::chrono::milliseconds poll_interval)
        : m_root(std::move(root).lexically_normal()), m_changes(m_root, poll_interval) {}

    AssetSourceMonitor::PollResult AssetSourceMonitor::poll_async(
        Comet::TaskScheduler& scheduler, const Clock::time_point now) {
        if(!m_initial_poll_attempted) {
            m_initial_poll_attempted = true;
            static_cast<void>(m_changes.poll());
            ++m_snapshot_generation;
            m_full_scan_requested = true;
        }

        const auto changes = m_changes.poll_changes(now);
        if(changes.reason == FileRecheckTrigger::Reason::Fallback) {
            if(!m_pending_snapshot) {
                ++m_snapshot_generation;
                m_full_scan_requested = true;
            }
        } else if(changes.requires_full_scan) {
            ++m_snapshot_generation;
            ++m_change_generation;
            m_full_scan_requested = true;
        } else if(changes.reason == FileRecheckTrigger::Reason::Notification) {
            ++m_change_generation;
            ++m_snapshot_generation;
            if(m_pending_snapshot || m_full_scan_requested) {
                m_full_scan_requested = true;
            } else {
                m_dirty_paths.insert(changes.paths.begin(), changes.paths.end());
                if(changes.paths.empty() || m_dirty_paths.size() > MAX_PENDING_PATHS)
                    m_full_scan_requested = true;
            }
        }

        PollResult result;
        if(m_pending_snapshot
            && m_pending_snapshot->completion.wait_for(std::chrono::seconds(0))
                   == std::future_status::ready) {
            auto snapshot_result = m_pending_snapshot->completion.get();
            // 新通知、显式刷新或内部写入使在途快照失效。
            if(m_pending_snapshot->generation == m_snapshot_generation) {
                result = accept_snapshot(std::move(snapshot_result));
                m_pending_files.reset();
                m_dirty_paths.clear();
            }
            m_pending_snapshot.reset();
        }

        if(m_full_scan_requested) {
            m_pending_files.reset();
            m_dirty_paths.clear();
        } else if(m_pending_files
                  && m_pending_files->completion.wait_for(std::chrono::seconds(0))
                         == std::future_status::ready) {
            const auto updates = m_pending_files->completion.get();
            if(!m_pending_snapshot
                && m_pending_files->generation == m_snapshot_generation) {
                result = accept_changed_files(updates);
                m_dirty_paths.clear();
                if(result.requires_full_scan)
                    m_full_scan_requested = true;
            }
            m_pending_files.reset();
        }

        if(m_full_scan_requested && !m_pending_snapshot) {
            auto completion =
                scheduler.try_submit_result([root = m_root] { return capture_snapshot(root); });
            if(completion) {
                m_pending_snapshot.emplace(std::move(*completion), m_snapshot_generation);
                m_full_scan_requested = false;
            }
        }
        if(!m_full_scan_requested && !m_pending_snapshot && !m_pending_files
            && !m_dirty_paths.empty()) {
            auto paths = std::vector(m_dirty_paths.begin(), m_dirty_paths.end());
            auto completion = scheduler.try_submit_result([root = m_root, paths = std::move(paths)] {
                return capture_changed_files(root, paths);
            });
            if(completion)
                m_pending_files.emplace(std::move(*completion), m_snapshot_generation);
        }
        return result;
    }

    AssetSourceMonitor::FileUpdates AssetSourceMonitor::capture_changed_files(
        const std::filesystem::path& root, const std::vector<std::filesystem::path>& paths) {
        PROFILE_SCOPE("AssetSourceMonitor::capture_changed_files");
        if(paths.empty())
            return {.requires_full_scan = true};

        std::error_code error;
        const auto absolute_root = std::filesystem::weakly_canonical(root, error);
        if(error)
            return {.requires_full_scan = true};

        FileUpdates result;
        for(const auto& path : paths) {
            const auto relative = path.lexically_normal().lexically_relative(absolute_root);
            if(!is_valid_relative_path(relative))
                return {.requires_full_scan = true};
            if(is_ignored_asset_source(relative))
                continue;

            const auto absolute_path = absolute_root / relative;
            if(!std::filesystem::is_regular_file(absolute_path, error) || error)
                return {.requires_full_scan = true};
            const auto write_time = std::filesystem::last_write_time(absolute_path, error);
            if(error)
                return {.requires_full_scan = true};
            const auto size = std::filesystem::file_size(absolute_path, error);
            if(error)
                return {.requires_full_scan = true};
            result.updates[relative] = FileState{.write_time = write_time, .size = size};
        }
        return result;
    }

    AssetSourceMonitor::PollResult AssetSourceMonitor::accept_changed_files(
        const FileUpdates& updates) {
        PROFILE_SCOPE("AssetSourceMonitor::accept_changed_files");
        if(updates.requires_full_scan)
            return {.requires_full_scan = true};
        for(const auto& update : updates.updates) {
            if(!m_snapshot.contains(update.first))
                return {.requires_full_scan = true};
        }
        PollResult result{.state = PollState::Unchanged};
        for(const auto& [path, state] : updates.updates) {
            if(m_snapshot.at(path) == state)
                continue;
            m_snapshot[path] = state;
            result.changed_paths.push_back(path);
        }
        if(!result.changed_paths.empty()) {
            result.state = PollState::Changed;
            ++m_snapshot_generation;
        }
        return result;
    }

    AssetSourceMonitor::PollResult AssetSourceMonitor::poll_now() {
        PROFILE_SCOPE("AssetSourceMonitor::poll_now");
        ++m_snapshot_generation;
        ++m_change_generation;
        m_full_scan_requested = false;
        m_dirty_paths.clear();
        if(!m_initial_poll_attempted) {
            m_initial_poll_attempted = true;
            static_cast<void>(m_changes.poll());
        }
        return accept_snapshot(capture_snapshot(m_root));
    }

    AssetSourceMonitor::PollResult AssetSourceMonitor::accept_snapshot(
        SnapshotResult snapshot_result) {
        PROFILE_SCOPE("AssetSourceMonitor::accept_snapshot");
        PollResult result;
        if(!snapshot_result) {
            result.state = PollState::Failed;
            result.issue_path = snapshot_result.error().path;
            result.message = snapshot_result.error().message;
            if(!m_has_baseline)
                m_initial_capture_failed = true;
            return result;
        }

        auto snapshot = std::move(snapshot_result).value();
        const bool changed = m_has_baseline ? snapshot != m_snapshot : m_initial_capture_failed;
        m_snapshot = std::move(snapshot);
        m_has_baseline = true;
        m_initial_capture_failed = false;
        if(changed)
            ++m_change_generation;
        result.state = changed ? PollState::Changed : PollState::Unchanged;
        result.requires_full_scan = true;
        return result;
    }

    bool AssetSourceMonitor::acknowledge(const std::filesystem::path& relative_path) {
        if(!m_has_baseline || !is_valid_relative_path(relative_path)
            || is_ignored_asset_source(relative_path)) {
            return false;
        }

        const std::filesystem::path normalized = relative_path.lexically_normal();
        const std::filesystem::path absolute_path = m_root / normalized;
        std::error_code error;
        const bool exists = std::filesystem::exists(absolute_path, error);
        if(error) {
            return false;
        }
        if(!exists) {
            m_snapshot.erase(normalized);
            ++m_snapshot_generation;
            ++m_change_generation;
            if(m_pending_snapshot) {
                m_full_scan_requested = true;
            }
            return true;
        }

        if(!std::filesystem::is_regular_file(absolute_path, error) || error) {
            return false;
        }
        const auto write_time = std::filesystem::last_write_time(absolute_path, error);
        if(error) {
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(absolute_path, error);
        if(error) {
            return false;
        }

        m_snapshot[normalized] = FileState{.write_time = write_time, .size = size};
        ++m_snapshot_generation;
        ++m_change_generation;
        if(m_pending_snapshot) {
            m_full_scan_requested = true;
        }
        return true;
    }

    AssetSourceMonitor::SnapshotResult AssetSourceMonitor::capture_snapshot(
        const std::filesystem::path& root) {
        PROFILE_SCOPE("AssetSourceMonitor::capture_snapshot");
        Snapshot snapshot;
        std::error_code error;
        const bool exists = std::filesystem::exists(root, error);
        if(error)
            return SnapshotResult::failure(
                {root, "failed to access assets directory: " + error.message()});
        if(!exists)
            return SnapshotResult::failure({root, "assets directory does not exist"});
        if(!std::filesystem::is_directory(root, error)) {
            if(error)
                return SnapshotResult::failure(
                    {root, "failed to access assets directory: " + error.message()});
            return SnapshotResult::failure({root, "assets path is not a directory"});
        }

        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::none, error);
        const std::filesystem::recursive_directory_iterator end;
        if(error)
            return SnapshotResult::failure(
                {root, "failed to scan assets directory: " + error.message()});

        while(iterator != end) {
            const std::filesystem::directory_entry entry = *iterator;
            const bool regular_file = entry.is_regular_file(error);
            if(error)
                return SnapshotResult::failure(
                    {entry.path(), "failed to inspect asset source: " + error.message()});

            if(regular_file && !is_ignored_asset_source(entry.path())) {
                const auto write_time = entry.last_write_time(error);
                if(error)
                    return SnapshotResult::failure({entry.path(),
                        "failed to read asset source write time: " + error.message()});
                const std::uintmax_t size = entry.file_size(error);
                if(error)
                    return SnapshotResult::failure(
                        {entry.path(), "failed to read asset source size: " + error.message()});

                snapshot.emplace(entry.path().lexically_relative(root).lexically_normal(),
                    FileState{.write_time = write_time, .size = size});
            }

            iterator.increment(error);
            if(error)
                return SnapshotResult::failure(
                    {entry.path(), "failed while scanning assets directory: " + error.message()});
        }
        return SnapshotResult::success(std::move(snapshot));
    }
}
