#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/result.h"

#include <memory>
#include <vector>

namespace Comet {
    class Device;
    class RenderPass;
    class RenderTarget;
    class FrameScheduler;
    class ImageView;
    class Pipeline;
    class DescriptorSetLayout;
    class Sampler;
    struct SampledImageBinding;

    // HDR 场景的最终输出 pass，不拥有场景、窗口或帧调度器。
    class COMET_API OutputPass {
    public:
        static Result<std::unique_ptr<OutputPass>, GraphicsError> create(Device& device,
            Format output_format, bool offscreen, uint32_t frame_slots,
            ImageColorSpace color_space = ImageColorSpace::SrgbNonlinearKHR,
            float hdr_headroom = 4.0f);
        ~OutputPass() = default;
        OutputPass(const OutputPass&) = delete;
        OutputPass& operator=(const OutputPass&) = delete;

        [[nodiscard]] RenderPass& get_render_pass() const { return *m_render_pass; }
        // 输入须为单采样 SampledRead；output 由本 pass 创建，Device 存活至 GPU 完成。
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            const std::shared_ptr<RenderTarget>& output,
            const std::shared_ptr<ImageView>& hdr_color, float exposure = 1.0f);

    private:
        OutputPass(Device& device, std::shared_ptr<RenderPass> pass,
            std::shared_ptr<DescriptorSetLayout> layout, std::shared_ptr<Sampler> sampler,
            std::shared_ptr<Pipeline> pipeline, uint32_t frame_slots, bool encode_srgb,
            bool offscreen, float headroom);

        Device& m_device;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<std::shared_ptr<SampledImageBinding>> m_bindings;
        bool m_encode_srgb;
        bool m_offscreen;
        float m_headroom;
    };
}
