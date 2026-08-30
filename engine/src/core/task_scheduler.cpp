#include "core/task_scheduler.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace Comet {
    namespace {
        std::size_t resolve_worker_count(const std::size_t requested_count) {
            if(requested_count > 0) {
                return requested_count;
            }

            const std::size_t hardware_threads = std::thread::hardware_concurrency();
            return std::max<std::size_t>(
                1,
                hardware_threads > 1 ? hardware_threads - 1 : 1);
        }
    }

    TaskScheduler::TaskScheduler(const std::size_t worker_count) {
        const std::size_t resolved_count = resolve_worker_count(worker_count);
        m_workers.reserve(resolved_count);
        try {
            for(std::size_t index = 0; index < resolved_count; ++index) {
                m_workers.emplace_back([this] { worker_loop(); });
            }
        } catch(...) {
            {
                const std::lock_guard lock(m_mutex);
                m_stopping = true;
            }
            m_task_available.notify_all();
            for(std::thread& worker: m_workers) {
                worker.join();
            }
            throw;
        }
    }

    TaskScheduler::~TaskScheduler() {
        {
            const std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_task_available.notify_all();
        for(std::thread& worker: m_workers) {
            if(worker.joinable()) {
                worker.join();
            }
        }
    }

    std::future<void> TaskScheduler::submit(Task task) {
        if(!task) {
            throw std::invalid_argument("Cannot submit an empty task");
        }

        auto scheduled_task = std::make_shared<std::packaged_task<void()>>(
            std::move(task));
        std::future<void> result = scheduled_task->get_future();
        {
            const std::lock_guard lock(m_mutex);
            if(m_stopping) {
                throw std::runtime_error(
                    "Cannot submit a task while the scheduler is stopping");
            }
            m_tasks.emplace_back([scheduled_task] { (*scheduled_task)(); });
        }
        m_task_available.notify_one();
        return result;
    }

    void TaskScheduler::wait_idle() {
        std::unique_lock lock(m_mutex);
        m_idle.wait(lock, [this] {
            return m_tasks.empty() && m_active_tasks == 0;
        });
    }

    std::size_t TaskScheduler::get_worker_count() const noexcept {
        return m_workers.size();
    }

    void TaskScheduler::worker_loop() {
        while(true) {
            Task task;
            {
                std::unique_lock lock(m_mutex);
                m_task_available.wait(lock, [this] {
                    return m_stopping || !m_tasks.empty();
                });
                if(m_stopping && m_tasks.empty()) {
                    return;
                }

                task = std::move(m_tasks.front());
                m_tasks.pop_front();
                ++m_active_tasks;
            }

            task();

            {
                const std::lock_guard lock(m_mutex);
                --m_active_tasks;
                if(m_tasks.empty() && m_active_tasks == 0) {
                    m_idle.notify_all();
                }
            }
        }
    }
}
