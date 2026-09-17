#include "asset/import/asset_task_queue.h"
#include "asset/import/import_candidate.h"
#include "common/scope_exit.h"
#include "core/task_scheduler.h"
#include "diagnostics/logger.h"

#include <algorithm>
#include <deque>
#include <future>
#include <unordered_map>
#include <vector>

namespace Comet {
    struct AssetTaskQueue::AsyncState {
        struct PendingAssetTask {
            AssetRevision revision;
            bool force_mesh_rebuild = false;
        };
        struct ScheduledAssetTask {
            AssetHandle handle;
            AssetRevision revision = INVALID_ASSET_REVISION;
            std::future<void> completion;
            std::shared_ptr<AssetImportResult> result;
        };
        struct QueuedAssetTask {
            AssetHandle handle;
            AssetRevision revision;
            std::function<void(AssetImportResult&)> task;
        };

        std::unordered_map<AssetHandle, PendingAssetTask> pending_assets;
        std::vector<ScheduledAssetTask> scheduled_tasks;
        std::deque<QueuedAssetTask> queued_tasks;
        bool processing_completions = false;

        bool has_queued_task(const AssetHandle handle, const AssetRevision revision) const {
            return std::ranges::any_of(queued_tasks, [&](const auto& request) {
                return request.handle == handle && request.revision == revision;
            });
        }
    };

    AssetTaskQueue::AssetTaskQueue(
        const AssetDatabase& database, TaskScheduler& scheduler, Limits limits)
        : m_database(database), m_task_scheduler(scheduler), m_async_limits(limits),
          m_async_state(std::make_unique<AsyncState>()) {
        if(limits.in_flight == 0 || limits.queued == 0)
            LOG_FATAL("Asset async limits must be positive");
        m_async_state->scheduled_tasks.reserve(limits.in_flight);
    }

    AssetTaskQueue::~AssetTaskQueue() {
        m_async_state->queued_tasks.clear();
        for(auto& task : m_async_state->scheduled_tasks)
            task.completion.wait();
    }

    AssetTaskQueue::Status AssetTaskQueue::status() const {
        return {m_async_state->scheduled_tasks.size(), m_async_state->queued_tasks.size()};
    }

    bool AssetTaskQueue::contains(const AssetHandle handle, const AssetRevision revision) const {
        const auto pending = m_async_state->pending_assets.find(handle);
        return pending != m_async_state->pending_assets.end()
               && pending->second.revision == revision;
    }

    Result<void, Error> AssetTaskQueue::process_completions(const CompletionBudget budget,
        const std::function<Result<void, Error>(AssetImportResult&)>& publish) {
        if(m_async_state->processing_completions) {
            LOG_WARN("Ignoring reentrant asset completion processing");
            return Result<void, Error>::success();
        }
        m_async_state->processing_completions = true;
        const ScopeExit reset_processing([&] { m_async_state->processing_completions = false; });

        const auto start = std::chrono::steady_clock::now();
        auto& tasks = m_async_state->scheduled_tasks;
        std::size_t processed = 0;
        for(auto task = tasks.begin(); task != tasks.end();) {
            if(processed >= budget.max_results
                || budget.max_time <= std::chrono::nanoseconds::zero()
                || (processed > 0 && std::chrono::steady_clock::now() - start >= budget.max_time))
                break;
            if(task->completion.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                ++task;
                continue;
            }
            ++processed;
            const auto pending = m_async_state->pending_assets.find(task->handle);
            if(pending != m_async_state->pending_assets.end()
                && pending->second.revision == task->revision
                && !m_async_state->has_queued_task(task->handle, task->revision))
                m_async_state->pending_assets.erase(pending);

            // 发布期间继续占槽；异常退出也必须移除已消费的 future。
            const ScopeExit remove_completed([&] { task = tasks.erase(task); });
            // 业务失败在候选 Result 中；get 同步写入并拒绝异常中断的候选。
            task->completion.get();
            if(!m_database.is_current(task->handle, task->revision)) {
                LOG_DEBUG("Discarded stale background asset {} (revision {})", task->handle.value(),
                    task->revision);
            } else {
                if(auto result = publish(*task->result); !result)
                    return result;
            }
        }
        dispatch_queued_tasks();
        return Result<void, Error>::success();
    }

    bool AssetTaskQueue::schedule(const AssetHandle handle, const AssetRevision revision,
        std::function<void(AssetImportResult&)> task, const bool force_mesh_rebuild) {
        const auto pending = m_async_state->pending_assets.find(handle);
        if(pending != m_async_state->pending_assets.end() && pending->second.revision == revision
            && (!force_mesh_rebuild || pending->second.force_mesh_rebuild)) {
            return true;
        }

        auto& queue = m_async_state->queued_tasks;
        const auto queued = std::ranges::find(queue, handle, &AsyncState::QueuedAssetTask::handle);
        if(queued == queue.end() && queue.size() >= m_async_limits.queued) {
            return false;
        }
        const auto [entry, inserted] = m_async_state->pending_assets.try_emplace(handle);
        ScopeExit undo_pending_insert([&] {
            if(inserted)
                m_async_state->pending_assets.erase(entry);
        });
        if(queued != queue.end())
            *queued = {handle, revision, std::move(task)};
        else
            queue.push_back({handle, revision, std::move(task)});
        entry->second = {.revision = revision, .force_mesh_rebuild = force_mesh_rebuild};
        undo_pending_insert.release();
        dispatch_queued_tasks();
        return true;
    }

    void AssetTaskQueue::dispatch_queued_tasks() {
        auto& queue = m_async_state->queued_tasks;
        auto& scheduled = m_async_state->scheduled_tasks;
        for(auto request = queue.begin();
            request != queue.end() && scheduled.size() < m_async_limits.in_flight;) {
            const auto clear_pending = [&] {
                const auto pending = m_async_state->pending_assets.find(request->handle);
                if(pending != m_async_state->pending_assets.end()
                    && pending->second.revision == request->revision)
                    m_async_state->pending_assets.erase(pending);
            };
            if(!m_database.is_current(request->handle, request->revision)) {
                clear_pending();
                request = queue.erase(request);
                continue;
            }
            if(std::ranges::any_of(
                   scheduled, [&](const auto& task) { return task.handle == request->handle; })) {
                ++request;
                continue;
            }
            auto result = std::make_shared<AssetImportResult>();
            auto completion =
                m_task_scheduler.try_submit([task = request->task, result] { task(*result); });
            if(!completion)
                break;
            // 构造时已预留全部在途槽位；接受任务后这里只移动完整所有者。
            scheduled.push_back(
                {request->handle, request->revision, std::move(*completion), std::move(result)});
            request = queue.erase(request);
        }
    }

}
