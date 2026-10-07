#include "render/overlay_record_context.h"
#include "render/frame_scheduler.h"

#include <utility>

namespace Comet {
    CommandBuffer& OverlayRecordContext::command_buffer() const {
        return m_frames.get_current_command_buffer();
    }

    uint32_t OverlayRecordContext::frame_slot() const {
        return m_frames.get_current_frame_slot_index();
    }

    uint64_t OverlayRecordContext::frame_serial() const {
        return m_frames.get_current_frame_serial();
    }

    void OverlayRecordContext::retain(std::shared_ptr<void> resource) {
        m_frames.retain_current_frame_resource(std::move(resource));
    }

    void OverlayRecordContext::wait_for(
        const GpuCompletionPoint& completion, const Flags<PipelineStage> stages) {
        if(completion.is_valid())
            merge_semaphore_wait(m_waits, QueueSemaphoreSubmit(completion, stages));
    }
}
