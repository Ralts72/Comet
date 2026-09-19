#pragma once

#include "core/math_utils.h"
#include "render/render_graph.h"

#include <array>

namespace Comet {
    class Device;
    class RenderPass;
    class RenderTarget;
    class ImageView;
    class Pipeline;
    class DescriptorSetLayout;
    class Sampler;
    struct SampledImageBinding;

    // Half-resolution HDR extraction and separable blur; final composition belongs to OutputPass.
    class COMET_API BloomPass {
    public:
        struct Passes {
            RenderGraph::ResourceId output;
            std::array<RenderGraph::PassId, 3> ids;
        };

        static Result<std::unique_ptr<BloomPass>, GraphicsError> create(
            Device& device, uint32_t frame_slots);
        static Passes append_passes(RenderGraph& graph, RenderGraph::ResourceId hdr);
        BloomPass(const BloomPass&) = delete;
        BloomPass& operator=(const BloomPass&) = delete;

        // Both candidates must succeed before replacing the current target generation.
        Result<void, GraphicsError> resize(Math::Vec2u source_size);
        void append_bindings(std::vector<RenderGraph::Binding>& bindings, uint32_t slot) const;
        [[nodiscard]] std::shared_ptr<ImageView> get_output(uint32_t slot) const;
        [[nodiscard]] Result<void, GraphicsError> render(FrameScheduler& frames,
            RenderGraph::PassId pass, const Passes& passes, const std::shared_ptr<ImageView>& hdr,
            float threshold);

    private:
        BloomPass(Device& device, uint32_t frame_slots);

        Device& m_device;
        uint32_t m_frame_slots;
        Math::Vec2u m_source_size{};
        std::shared_ptr<RenderPass> m_render_pass;
        std::shared_ptr<DescriptorSetLayout> m_layout;
        std::shared_ptr<Sampler> m_sampler;
        std::shared_ptr<Pipeline> m_pipeline;
        std::array<std::shared_ptr<RenderTarget>, 2> m_targets;
        std::vector<std::array<std::shared_ptr<SampledImageBinding>, 3>> m_bindings;
    };
}
