#pragma once

#include "core/math_utils.h"
#include "render/render_graph.h"

#include <array>

namespace Comet {
    class Device;
    class RenderPass;
    class ImageView;
    class Pipeline;
    class DescriptorSetLayout;
    class Sampler;
    struct SampledImageBinding;

    // 半分辨率 HDR 高亮提取与横纵模糊；最终合成由 OutputPass 负责。
    class COMET_API BloomPass {
    public:
        struct Passes {
            RenderGraph::ResourceId output;
            RenderGraph::ResourceId extracted;
            RenderGraph::ResourceId intermediate;
            std::array<RenderGraph::PassId, 3> ids;
            bool operator==(const Passes&) const = default;
        };

        static Result<std::unique_ptr<BloomPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        static Passes append_passes(
            RenderGraph& graph, RenderGraph::ResourceId hdr, Math::Vec2u source_size);
        BloomPass(const BloomPass&) = delete;
        BloomPass& operator=(const BloomPass&) = delete;

        // 图分配和 framebuffer 全部成功后才发布；旧代由在途帧保活。
        Result<void, GraphicsError> prepare(
            const RenderGraph::Resources& resources, const Passes& passes, Math::Vec2u source_size);
        [[nodiscard]] std::shared_ptr<ImageView> get_output(uint32_t slot) const;
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            RenderGraph::PassId pass, const Passes& passes, const std::shared_ptr<ImageView>& hdr,
            float threshold);

    private:
        BloomPass(Device& device, uint32_t frame_slots);

        Device& m_device;
        uint32_t m_frame_slots;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        struct Targets;
        std::shared_ptr<Targets> m_targets;
        std::vector<std::array<std::shared_ptr<SampledImageBinding>, 3>> m_bindings;
    };
}
