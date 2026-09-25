#pragma once

#include "common/export.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace Comet {
    class COMET_API TaskScheduler final {
    public:
        using Task = std::function<void()>;

        // 容量必须为正；worker_count 为 0 时按硬件线程数选择。
        explicit TaskScheduler(std::size_t worker_count = 0, std::size_t queue_capacity = 128);
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;
        TaskScheduler(TaskScheduler&&) = delete;
        TaskScheduler& operator=(TaskScheduler&&) = delete;

        // 空任务、队列满或停止接收时立即返回空，不在提交线程执行任务或等待容量。
        [[nodiscard]] std::optional<std::future<void>> try_submit(Task task);
        template<typename Function>
        [[nodiscard]] auto try_submit_result(Function&& function)
            -> std::optional<std::future<std::invoke_result_t<Function&>>> {
            using Value = std::invoke_result_t<Function&>;
            auto task =
                std::make_shared<std::packaged_task<Value()>>(std::forward<Function>(function));
            auto result = task->get_future();
            if(!enqueue([task] { (*task)(); }))
                return std::nullopt;
            return result;
        }
        void wait_idle();
        // Owner 线程调用；停止接收并排空任务，可重复调用，不可由 Worker 调用。
        void shutdown();

        [[nodiscard]] std::size_t get_worker_count() const noexcept;
        [[nodiscard]] std::size_t get_queue_capacity() const noexcept { return m_queue_capacity; }

    private:
        [[nodiscard]] bool enqueue(Task task);
        void worker_loop();

        std::mutex m_mutex;
        std::condition_variable m_task_available;
        std::condition_variable m_idle;
        std::deque<Task> m_tasks;
        std::vector<std::thread> m_workers;
        std::size_t m_queue_capacity;
        std::size_t m_active_tasks = 0;
        bool m_stopping = false;
    };
}
