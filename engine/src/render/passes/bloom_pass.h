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

        struct Resources;
        // 返回独立候选；图 owner 在整份计划成功后提交，旧代由在途帧保活。
        [[nodiscard]] Result<std::shared_ptr<Resources>, GraphicsError> create_resources(
            const RenderGraph::Resources& resources, const Passes& passes,
            Math::Vec2u source_size) const;
        [[nodiscard]] static std::shared_ptr<ImageView> output(const Resources&, uint32_t slot);
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            RenderGraph::PassId pass, const std::shared_ptr<Resources>& resources,
            const std::shared_ptr<ImageView>& hdr, float threshold);

    private:
        BloomPass(Device& device, uint32_t frame_slots);

        Device& m_device;
        uint32_t m_frame_slots;
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
    };
}
