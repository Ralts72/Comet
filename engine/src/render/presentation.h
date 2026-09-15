#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/queue.h"
#include "graphics/swapchain.h"
#include "common/retry_backoff.h"

#include <functional>
#include <span>

namespace Comet {
    class RenderContext;
    class FrameScheduler;
    struct SwapchainCompatibility;

    class COMET_API Presentation {
    public:
        struct Dependent {
            std::function<void()> release;
            std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild;
        };

        Presentation(RenderContext& context, FrameScheduler& frames, Dependent scene);
        void set_overlay(Dependent overlay);
        // 成功值 false 表示延期；错误保留原生状态码。
        [[nodiscard]] Result<bool, GraphicsError> begin_frame();
        [[nodiscard]] Result<void, GraphicsError> end_frame(
            std::span<const QueueSemaphoreSubmit> resource_waits);
        void request_recreation();

    private:
        enum class RecoveryStage { Ready, Swapchain, Dependents, Surface };
        Result<void, GraphicsError> recover();
        Result<void, GraphicsError> handle_failure(const GraphicsError& error);
        void release_dependents();
        RecoveryStage m_recovery = RecoveryStage::Ready;
        RetryBackoff m_retry;
        bool m_retry_pending = false;
        SwapchainConfig m_installed_config;
        RenderContext& m_context;
        FrameScheduler& m_frames;
        Dependent m_scene;
        Dependent m_overlay;
    };
}
