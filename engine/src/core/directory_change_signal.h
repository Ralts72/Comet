#pragma once

#include "common/export.h"

#include <chrono>
#include <filesystem>
#include <memory>

namespace Comet {
    // 文件通知只是线索；消费方仍须复核自己的源码快照。
    class COMET_API DirectoryChangeSignal final {
    public:
        using Clock = std::chrono::steady_clock;

        explicit DirectoryChangeSignal(std::filesystem::path root,
            std::chrono::milliseconds fallback_interval = std::chrono::milliseconds(500));
        ~DirectoryChangeSignal();

        DirectoryChangeSignal(const DirectoryChangeSignal&) = delete;
        DirectoryChangeSignal& operator=(const DirectoryChangeSignal&) = delete;

        [[nodiscard]] bool poll(Clock::time_point now = Clock::now());
        [[nodiscard]] bool uses_native_notifications() const;

    private:
        struct Backend;
        std::unique_ptr<Backend> m_backend;
        std::chrono::milliseconds m_fallback_interval;
        Clock::time_point m_next_fallback{};
    };
}
