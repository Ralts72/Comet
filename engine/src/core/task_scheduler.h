#pragma once

#include "common/export.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace Comet {
    class COMET_API TaskScheduler final {
    public:
        using Task = std::function<void()>;

        explicit TaskScheduler(std::size_t worker_count = 0);
        ~TaskScheduler();

        TaskScheduler(const TaskScheduler&) = delete;
        TaskScheduler& operator=(const TaskScheduler&) = delete;
        TaskScheduler(TaskScheduler&&) = delete;
        TaskScheduler& operator=(TaskScheduler&&) = delete;

        [[nodiscard]] std::future<void> submit(Task task);
        void wait_idle();

        [[nodiscard]] std::size_t get_worker_count() const noexcept;

    private:
        void worker_loop();

        std::mutex m_mutex;
        std::condition_variable m_task_available;
        std::condition_variable m_idle;
        std::deque<Task> m_tasks;
        std::vector<std::thread> m_workers;
        std::size_t m_active_tasks = 0;
        bool m_stopping = false;
    };
}
