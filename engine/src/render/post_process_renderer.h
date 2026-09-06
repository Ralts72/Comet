#pragma once

#include "common/export.h"
#include "graphics/enums.h"
#include "graphics/resource/resource_result.h"
#include "render/render_graph.h"
#include "core/math_utils.h"

#include <array>
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

    // HDR 后处理链；不拥有场景、窗口或帧调度器。
    class COMET_API PostProcessRenderer {
    public:
        struct Settings {
            float exposure = 1.0f;
            float bloom_strength = 0.0f;
            float bloom_threshold = 1.0f;
            [[nodiscard]] bool is_valid() const;
            [[nodiscard]] bool uses_bloom() const { return bloom_strength > 0; }
        };
        PostProcessRenderer(
            Device& device, Format output_format, bool offscreen, uint32_t frame_slots);
        ~PostProcessRenderer() = default;
        PostProcessRenderer(const PostProcessRenderer&) = delete;
        PostProcessRenderer& operator=(const PostProcessRenderer&) = delete;

        [[nodiscard]] RenderPass& get_render_pass() const { return *m_render_pass; }
        // 图中的资源和录制顺序由此模块共同定义，不暴露 ping-pong target。
        static void append_passes(
            RenderGraph& graph, RenderGraph::ResourceId hdr, bool bloom);
        // 以 append_passes 导入资源的相同顺序追加。
        void append_bindings(
            std::vector<RenderGraph::Binding>& bindings, uint32_t slot, bool bloom) const;
        // size 是完整 HDR 尺寸；两张候选图整体替换，失败保留旧图。
        [[nodiscard]] GpuResourceResult<void> try_resize_bloom(Math::Vec2u size);
        void render_pass(size_t pass, FrameScheduler& frames,
            const std::shared_ptr<RenderTarget>& output, uint32_t output_index,
            const std::shared_ptr<ImageView>& hdr_color, const Settings& settings);
        // 输入必须单采样且处于 SampledRead；output 必须由 get_render_pass() 创建。
        // 输出清除与最终转换由本通道负责，调用方在 GPU 完成前保持 Device 存活。
        void render(FrameScheduler& frames, const std::shared_ptr<RenderTarget>& output,
            uint32_t output_index, const std::shared_ptr<ImageView>& hdr_color,
            float exposure = 1.0f);

    private:
        struct Binding;
        struct Parameters;
        void draw(FrameScheduler& frames, size_t binding_index,
            const std::shared_ptr<Pipeline>& pipeline,
            const std::shared_ptr<RenderPass>& render_pass,
            const std::shared_ptr<RenderTarget>& output, uint32_t output_index,
            const std::shared_ptr<ImageView>& first,
            const std::shared_ptr<ImageView>& second, const Parameters& parameters);
        Device& m_device;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<RenderPass> m_bloom_pass;
        std::shared_ptr<Pipeline> m_bloom_pipeline;
        std::array<std::shared_ptr<RenderTarget>, 2> m_bloom_targets;
        std::vector<std::array<std::shared_ptr<Binding>, 4>> m_bindings;
        bool m_encode_srgb;
    };
}
