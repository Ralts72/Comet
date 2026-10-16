#pragma once

#include "render/render_graph.h"
#include "render/scene/render_types.h"
#include <memory>

namespace Comet {
    class DescriptorSet;
    class DescriptorSetLayout;
    class PipelineManager;
    class Pipeline;
    class ImageView;
    struct RenderSubmission;

    struct FogVolume {
        std::shared_ptr<ImageView> image;
        Math::Vec4 parameters{}; // near (zero disables), inverse log range, inverse screen size
    };

    // Camera-space single scattering. The graph owns atlas images; frame slots own bindings.
    class COMET_API VolumetricFogPass {
    public:
        struct Resources;
        static Result<std::unique_ptr<VolumetricFogPass>, GraphicsError> create(Device& device,
            PipelineManager& pipelines, std::shared_ptr<DescriptorSetLayout> view_layout,
            SampleCount samples);
        ~VolumetricFogPass();
        static Math::Vec2u atlas_size(Math::Vec2u scene_size);
        Result<std::shared_ptr<Resources>, GraphicsError> create_resources(
            const RenderGraph::Resources& images, RenderGraph::ResourceId atlas, uint32_t slots);
        FogVolume prepare_frame(const Resources& resources, uint32_t slot,
            const RenderSubmission& submission, Math::Vec2u size) const;
        void render(FrameScheduler& frames, const Resources& resources, const DescriptorSet& view);
        void render_background(FrameScheduler& frames, const DescriptorSet& view, Math::Vec2u size);

    private:
        struct Program;
        explicit VolumetricFogPass(Device& device);
        Device& m_device;
        std::shared_ptr<Program> m_program;
        std::shared_ptr<Pipeline> m_background;
    };
}
