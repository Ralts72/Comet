#pragma once
#include "render_context.h"
#include "resource_manager.h"
#include "graphics/pipeline.h"
#include "frame_manager.h"
#include "graphics/render_pass.h"
#include "graphics/descriptor_set.h"
#include "graphics/buffer.h"
#include "render_target.h"
#include "common/shader_resources.h"
#include <memory>

namespace Comet {
    class SceneRenderer {
    public:
        SceneRenderer(RenderContext* context, ResourceManager* resources, RenderPass* render_pass);
        
        void render(const ViewProjectMatrix& view_project, const ModelMatrix& model,
                   std::shared_ptr<Mesh> mesh, std::shared_ptr<Pipeline> pipeline,
                   const std::vector<DescriptorSet>& descriptor_sets);
        
        uint32_t begin_frame();
        void end_frame();
        
        [[nodiscard]] FrameManager* get_frame_manager() const { return m_frame_manager.get(); }
        [[nodiscard]] RenderTarget* get_render_target() const { return m_render_target.get(); }
        [[nodiscard]] PipelineManager* get_pipeline_manager() const { return m_pipeline_manager.get(); }
        
        void recreate_swapchain();
        void update_descriptor_sets(const std::vector<DescriptorSet>& descriptor_sets,
                                   std::shared_ptr<Buffer> view_project_buffer,
                                   std::shared_ptr<Buffer> model_buffer,
                                   std::shared_ptr<Texture> texture1,
                                   std::shared_ptr<Texture> texture2,
                                   SamplerManager* sampler_manager);
        
    private:
        RenderContext* m_context;
        ResourceManager* m_resources;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameManager> m_frame_manager;
        std::shared_ptr<RenderTarget> m_render_target;
        RenderPass* m_render_pass;
    };
}

