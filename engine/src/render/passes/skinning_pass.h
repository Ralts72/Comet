#pragma once
#include "graphics/result.h"
#include "graphics/queue.h"
#include "render/scene/render_submission.h"
#include <memory>
#include <unordered_map>
namespace Comet {
    class Device;
    class FrameScheduler;
    class RenderGeometry;
    class COMET_API SkinningPass {
    public:
        struct Statistics {
            uint32_t dispatches = 0;
            uint32_t reused_outputs = 0;
            uint64_t palette_upload_bytes = 0;
        };
        explicit SkinningPass(Device& device, uint32_t frame_slots);
        ~SkinningPass();
        [[nodiscard]] Statistics get_statistics() const { return m_statistics; }
        Result<void, GraphicsError> prepare(const RenderWorld& world, FrameScheduler& frames,
            RenderGeometry& geometry, std::vector<QueueSemaphoreSubmit>& waits);

    private:
        struct Program;
        struct Instance;
        Result<void, GraphicsError> initialize();
        Result<std::shared_ptr<Instance>, GraphicsError> create_instance(
            const std::shared_ptr<Mesh>& source);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::vector<std::unordered_map<std::size_t, std::shared_ptr<Instance>>> m_frames;
        std::vector<ResolvedRenderItem> m_items;
        std::vector<std::size_t> m_slots;
        Statistics m_statistics;
    };
}
