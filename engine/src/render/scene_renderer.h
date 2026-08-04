#pragma once
#include "common/config.h"
#include "common/export.h"
#include "common/shader_resources.h"
#include "core/math_utils.h"
#include "frame_manager.h"
#include "graphics/buffer.h"
#include "graphics/descriptor_set.h"
#include "graphics/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/sampler.h"
#include "graphics/vertex_description.h"
#include "mesh.h"
#include "render_context.h"
#include "render_submission.h"
#include "render_target.h"
#include "texture.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

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

        SceneRenderer(RenderContext& context,
                      const Config::Vulkan& vulkan_config,
                      const Config::Render& render_config);

        void setup_render_pass();

        std::shared_ptr<DescriptorSetLayout> create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings);

        void setup_pipeline(ResourceManager& resource_manager,
                            const ShaderLayout& layout,
                            const VertexInputDescription& vertex_input,
                            const PipelineConfig& config);

        void render(const RenderSubmission& submission,
                    const ViewProjectMatrix& view_project_matrix);

        uint32_t begin_frame();

        void end_frame();

        void end_render_pass() const;

        void set_render_mode(RenderMode mode);

        [[nodiscard]] RenderMode get_render_mode() const { return m_render_mode; }
        [[nodiscard]] FrameManager& get_frame_manager() { return *m_frame_manager; }
        [[nodiscard]] const FrameManager& get_frame_manager() const { return *m_frame_manager; }
        [[nodiscard]] RenderTarget& get_render_target() { return *m_render_target; }
        [[nodiscard]] const RenderTarget& get_render_target() const { return *m_render_target; }
        [[nodiscard]] PipelineManager& get_pipeline_manager() { return *m_pipeline_manager; }
        [[nodiscard]] const PipelineManager& get_pipeline_manager() const { return *m_pipeline_manager; }
        [[nodiscard]] RenderPass& get_render_pass() { return *m_render_pass; }
        [[nodiscard]] const RenderPass& get_render_pass() const { return *m_render_pass; }
        [[nodiscard]] const std::shared_ptr<Pipeline>& get_pipeline() const { return m_pipeline; }
        [[nodiscard]] CommandBuffer& get_current_command_buffer() const;

        void recreate_swapchain();

        using SwapchainRecreateCallback = std::function<void()>;
        void set_swapchain_recreate_callback(SwapchainRecreateCallback callback) {
            m_swapchain_recreate_callback = std::move(callback);
        }

    private:
        struct DescriptorResources {
            std::shared_ptr<Buffer> view_project_buffer;
            std::array<std::shared_ptr<Texture>, 2> textures;
        };

        struct MaterialDescriptorState {
            std::shared_ptr<DescriptorPool> pool;
            std::vector<DescriptorSet> descriptor_sets;
            std::vector<DescriptorResources> resources;
        };

        [[nodiscard]] const DescriptorSet& prepare_material_descriptor_set(
            const MaterialBinding& material,
            const std::shared_ptr<Buffer>& view_project_buffer,
            const Sampler& sampler);

        void render_item(const ResolvedRenderItem& render_item,
                         const DescriptorSet& descriptor_set) const;

        void update_descriptor_set(const DescriptorSet& descriptor_set,
                                   const DescriptorResources& resources,
                                   const Sampler& sampler) const;

        SwapchainRecreateCallback m_swapchain_recreate_callback;
        RenderContext& m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameManager> m_frame_manager;
        std::unique_ptr<RenderTarget> m_render_target;
        RenderMode m_render_mode = RenderMode::Runtime;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<Sampler> m_default_sampler;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::unordered_map<AssetHandle, MaterialDescriptorState> m_material_descriptors;
        std::vector<std::shared_ptr<Buffer>> m_view_project_uniform_buffers;
        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
