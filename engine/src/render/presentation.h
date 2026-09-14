#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/queue.h"

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
        [[nodiscard]] bool begin_frame();
        void end_frame(std::span<const QueueSemaphoreSubmit> resource_waits);
        [[nodiscard]] bool recreate_swapchain();

    private:
        RenderContext& m_context;
        FrameScheduler& m_frames;
        Dependent m_scene;
        Dependent m_overlay;
    };
}
