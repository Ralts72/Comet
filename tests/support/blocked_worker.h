#pragma once

#include "core/task_scheduler.h"

#include <future>
#include <memory>
#include <gtest/gtest.h>

namespace Comet::Tests {
    // 确认 Worker 已占用，并在断言提前退出时也解除阻塞。
    class BlockedWorker {
    public:
        explicit BlockedWorker(TaskScheduler& scheduler) {
            auto started = std::make_shared<std::promise<void>>();
            auto entered = started->get_future();
            auto completion =
                scheduler.try_submit([started, gate = m_release.get_future().share()] {
                    started->set_value();
                    gate.wait();
                });
            if(!completion) {
                ADD_FAILURE() << "Failed to submit blocking worker";
                return;
            }
            m_completion = std::move(*completion);
            entered.wait();
        }

        ~BlockedWorker() { release(); }

        void release() {
            if(m_completion.valid()) {
                m_release.set_value();
                m_completion.get();
            }
        }

    private:
        std::promise<void> m_release;
        std::future<void> m_completion;
    };
}
