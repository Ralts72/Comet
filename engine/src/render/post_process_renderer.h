#pragma once

#include "common/export.h"
#include "graphics/enums.h"

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

    // 固定的 HDR -> SDR 输出通道；不拥有场景、窗口或帧调度器。
    class COMET_API PostProcessRenderer {
    public:
        PostProcessRenderer(
            Device& device, Format output_format, bool offscreen, uint32_t frame_slots);
        ~PostProcessRenderer() = default;
        PostProcessRenderer(const PostProcessRenderer&) = delete;
        PostProcessRenderer& operator=(const PostProcessRenderer&) = delete;

        [[nodiscard]] RenderPass& get_render_pass() const { return *m_render_pass; }
        // 输入必须单采样且处于 SampledRead；output 必须由 get_render_pass() 创建。
        // 输出清除与最终转换由本通道负责，调用方在 GPU 完成前保持 Device 存活。
        void render(FrameScheduler& frames, const std::shared_ptr<RenderTarget>& output,
            uint32_t output_index, const std::shared_ptr<ImageView>& hdr_color,
            float exposure = 1.0f);

    private:
        struct Binding;
        Device& m_device;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::vector<std::shared_ptr<Binding>> m_bindings;
        bool m_encode_srgb;
    };
}
