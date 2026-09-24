#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

namespace CometEditor {
    // 文件通知只是线索；消费方仍须复核自己的源码快照。
    class FileRecheckTrigger final {
    public:
        using Clock = std::chrono::steady_clock;
        enum class Reason { None, Notification, Fallback };

        explicit FileRecheckTrigger(std::filesystem::path root,
            std::chrono::milliseconds fallback_interval = std::chrono::milliseconds(500));
        ~FileRecheckTrigger();

        FileRecheckTrigger(const FileRecheckTrigger&) = delete;
        FileRecheckTrigger& operator=(const FileRecheckTrigger&) = delete;

        [[nodiscard]] Reason poll(Clock::time_point now = Clock::now());
        [[nodiscard]] bool uses_native_notifications() const;

    private:
        struct Backend;
        std::unique_ptr<Backend> m_backend;
        std::chrono::milliseconds m_fallback_interval;
        Clock::time_point m_next_fallback{};
    };
}
