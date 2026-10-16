#pragma once

#include "graphics/result.h"
#include "render/scene/render_types.h"
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Device;
    class FrameScheduler;
    class ImageView;
    class Pipeline;
    class RenderPass;
    class RenderTarget;
    class DescriptorSetLayout;
    class Sampler;
    struct SampledImageBinding;

    // 一个视图的时域历史。只消费 RenderThread 的真实呈现顺序，不推进模拟。
    class COMET_API TemporalPass {
    public:
        static Result<std::unique_ptr<TemporalPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        ~TemporalPass();
        struct Camera {
            ViewProjectMatrix current;
            std::optional<Math::Mat4> previous;
            Math::Vec4 previous_projection{};
        };
        Result<Camera, GraphicsError> prepare(FrameScheduler& frames, uint64_t scene_lifetime,
            const ViewProjectMatrix& camera, Math::Vec2u size);
        Result<void, GraphicsError> render(FrameScheduler& frames,
            const std::shared_ptr<ImageView>& color, const std::shared_ptr<ImageView>& depth,
            const std::shared_ptr<ImageView>& motion);
        [[nodiscard]] std::shared_ptr<ImageView> output() const;
        [[nodiscard]] std::shared_ptr<ImageView> history() const;
        void reset();
        [[nodiscard]] bool history_valid() const { return m_history_valid; }

    private:
        explicit TemporalPass(Device& device);
        Device& m_device;
        std::shared_ptr<RenderPass> m_pass;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<RenderTarget> m_target;
        std::vector<std::shared_ptr<SampledImageBinding>> m_bindings;
        ViewProjectMatrix m_camera{Math::Mat4(1), Math::Mat4(1)};
        ViewProjectMatrix m_unjittered{Math::Mat4(1), Math::Mat4(1)};
        uint64_t m_lifetime = 0;
        uint64_t m_recorded_serial = 0;
        uint64_t m_initialized_serial = 0;
        uint32_t m_index = 0;
        uint32_t m_sample = 0;
        Math::Vec2 m_jitter{};
        Math::Vec2 m_jitter_delta{};
        bool m_history_valid = false;
    };
}
