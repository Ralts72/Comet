#pragma once

#include "common/export.h"
#include "graphics/queue.h"

#include <memory>

namespace Comet {
    class CommandBuffer;
    class FrameScheduler;
    class Renderer;

    // 只在当前帧的 Overlay 回调内有效。
    class COMET_API OverlayRecordContext {
    public:
        OverlayRecordContext(const OverlayRecordContext&) = delete;
        OverlayRecordContext& operator=(const OverlayRecordContext&) = delete;
        [[nodiscard]] CommandBuffer& command_buffer() const;
        [[nodiscard]] uint32_t frame_slot() const;
        [[nodiscard]] uint64_t frame_serial() const;
        void retain(std::shared_ptr<void> resource);
        void wait_for(const GpuCompletionPoint& completion, Flags<PipelineStage> stages);

    private:
        friend class Renderer;
        OverlayRecordContext(FrameScheduler& frames, std::vector<QueueSemaphoreSubmit>& waits)
            : m_frames(frames), m_waits(waits) {}

        FrameScheduler& m_frames;
        std::vector<QueueSemaphoreSubmit>& m_waits;
    };
}
