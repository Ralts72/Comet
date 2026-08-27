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

        SceneRenderer(RenderContext& context,
                      const Config::Vulkan& vulkan_config,
                      const Config::Render& render_config);

        void setup_render_pass();

        void setup_viewport_render_pass(Math::Vec2u size);

        std::shared_ptr<DescriptorSetLayout> create_descriptor_set_layout(const DescriptorSetLayoutBindings& bindings);

        void setup_pipeline(ResourceManager& resource_manager,
                            const ShaderLayout& layout,
                            const VertexInputDescription& vertex_input,
                            const PipelineConfig& config);

        void render(const RenderSubmission& submission);

        [[nodiscard]] bool begin_frame();

        void end_frame();

        void end_render_pass() const;

        void request_viewport_resize(Math::Vec2u size);

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
        [[nodiscard]] bool is_viewport_rendering() const { return m_uses_viewport_target; }
        [[nodiscard]] std::vector<vk::ImageView> get_viewport_color_views() const;

        [[nodiscard]] bool recreate_swapchain();

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

        void reset_render_pipeline();
        void set_render_target_clear_color() const;
        void apply_pending_viewport_resize();

        SwapchainRecreateCallback m_swapchain_recreate_callback;
        RenderContext& m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameManager> m_frame_manager;
        std::unique_ptr<RenderTarget> m_render_target;
        bool m_uses_viewport_target = false;
        Math::Vec2u m_requested_viewport_size = Math::Vec2u(0);
        uint32_t m_viewport_size_stable_frames = 0;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<Sampler> m_default_sampler;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::unordered_map<AssetHandle, MaterialDescriptorState> m_material_descriptors;
        std::vector<std::shared_ptr<Buffer>> m_view_project_uniform_buffers;
        Config::Vulkan m_vulkan_config;
        Config::Render m_render_config;
    };
}
