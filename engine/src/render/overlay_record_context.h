#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include "graphics/queue.h"

#include <memory>
#include <vector>

namespace Comet {
    class OverlayRecordContext;

    // Main 捕获的独立绘制数据；后端须存活到录制完成。
    class COMET_API OverlayDraw {
    public:
        virtual ~OverlayDraw() = default;
        [[nodiscard]] virtual Result<void, GraphicsError> record(OverlayRecordContext& frame) = 0;
    };

    using OverlayDrawList = std::vector<std::unique_ptr<OverlayDraw>>;

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
