#pragma once

#include "asset/database.h"
#include "asset/asset_manager.h"
#include "common/error.h"
#include "common/result.h"

#include <chrono>
#include <functional>
#include <memory>

namespace Comet {
    class TaskScheduler;
    struct AssetImportResult;

    // owner 线程管理请求与发布，Worker 只写独占候选。
    class AssetTaskQueue {
    public:
        using Limits = AssetManager::AsyncLimits;
        using Status = AssetManager::AsyncStatus;
        using CompletionBudget = AssetManager::CompletionBudget;
        AssetTaskQueue(const AssetDatabase& database, TaskScheduler& scheduler, Limits limits);
        ~AssetTaskQueue();
        [[nodiscard]] Status status() const;
        [[nodiscard]] bool contains(AssetHandle handle, AssetRevision revision) const;
        [[nodiscard]] bool schedule(AssetHandle handle, AssetRevision revision,
            std::function<void(AssetImportResult&)> task, bool force_mesh_rebuild = false);
        Result<void, Error> process_completions(CompletionBudget budget,
            const std::function<Result<void, Error>(AssetImportResult&)>& publish);

    private:
        struct AsyncState;
        void dispatch_queued_tasks();
        const AssetDatabase& m_database;
        TaskScheduler& m_task_scheduler;
        Limits m_async_limits;
        std::unique_ptr<AsyncState> m_async_state;
    };
}
