#pragma once
#include "render_context.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "frame_manager.h"
#include "graphics/descriptor_set.h"
#include "graphics/buffer.h"
#include "render_target.h"
#include "common/shader_resources.h"
#include "texture.h"
#include "mesh.h"
#include "graphics/sampler.h"
#include "graphics/vertex_description.h"
#include "common/export.h"

namespace Comet {
    class ResourceManager;
    class VertexInputDescription;

    class COMET_API SceneRenderer {
    public:
        enum class RenderMode {
            Runtime,
            SceneView,
            GameView
        };

        explicit SceneRenderer(RenderContext* context);

        void setup_render_pass();

        std::shared_ptr<DescriptorSetLayout> create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings);

        void setup_pipeline(const ResourceManager* resource_manager,
                            const ShaderLayout& layout,
                            const VertexInputDescription& vertex_input,
                            const PipelineConfig& config);

        void setup_descriptor_sets(const DescriptorSetLayoutBindings& bindings);

        void render(const ViewProjectMatrix& view_project, const ModelMatrix& model,
                    const std::shared_ptr<Mesh>& mesh,
                    const std::vector<DescriptorSet>& descriptor_sets) const;

        uint32_t begin_frame();

        void end_frame();

        void end_render_pass() const;

        void set_render_mode(RenderMode mode);

        [[nodiscard]] RenderMode get_render_mode() const { return m_render_mode; }
        [[nodiscard]] FrameManager* get_frame_manager() const { return m_frame_manager.get(); }
        [[nodiscard]] RenderTarget* get_render_target() const { return m_render_target.get(); }
        [[nodiscard]] PipelineManager* get_pipeline_manager() const { return m_pipeline_manager.get(); }
        [[nodiscard]] RenderPass* get_render_pass() const { return m_render_pass.get(); }
        [[nodiscard]] std::shared_ptr<Pipeline> get_pipeline() const { return m_pipeline; }
        [[nodiscard]] const std::vector<DescriptorSet>& get_descriptor_sets() const { return m_descriptor_sets; }
        [[nodiscard]] CommandBuffer& get_current_command_buffer() const;

        void recreate_swapchain();

        using SwapchainRecreateCallback = std::function<void()>;
        void set_swapchain_recreate_callback(SwapchainRecreateCallback callback) {
            m_swapchain_recreate_callback = std::move(callback);
        }

        void update_descriptor_sets(const std::vector<DescriptorSet>& descriptor_sets,
                                    const std::shared_ptr<Buffer>& view_project_buffer,
                                    const std::shared_ptr<Buffer>& model_buffer,
                                    const std::shared_ptr<Texture>& texture1,
                                    const std::shared_ptr<Texture>& texture2,
                                    SamplerManager* sampler_manager) const;

    private:
        SwapchainRecreateCallback m_swapchain_recreate_callback;
        RenderContext* m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameManager> m_frame_manager;
        std::shared_ptr<RenderTarget> m_render_target;
        RenderMode m_render_mode = RenderMode::Runtime;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<DescriptorPool> m_descriptor_pool;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::vector<DescriptorSet> m_descriptor_sets;
    };
}

