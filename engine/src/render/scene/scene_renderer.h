#pragma once
#include "config/config.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "render/frame_manager.h"
#include "graphics/queue.h"
#include "graphics/resource/buffer.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/enums.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/resource/sampler.h"
#include "graphics/pipeline/vertex_description.h"
#include "graphics/vk_common.h"
#include "render/resource/mesh.h"
#include "render/render_context.h"
#include "render/scene/render_submission.h"
#include "render/render_target.h"
#include "render/resource/texture.h"

#include <array>
#include <functional>
#include <memory>
#include <span>
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
                            const PipelineConfig& config);

        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render(
            const RenderSubmission& submission);

        [[nodiscard]] bool begin_frame();

        void end_frame(
            std::span<const QueueSemaphoreSubmit> resource_waits);

        void end_render_pass() const;

        void request_viewport_resize(Math::Vec2u size);

        [[nodiscard]] FrameManager& get_frame_manager() { return *m_frame_manager; }
        [[nodiscard]] const FrameManager& get_frame_manager() const { return *m_frame_manager; }
        [[nodiscard]] RenderTarget& get_render_target() { return *m_render_target; }
        [[nodiscard]] const RenderTarget& get_render_target() const { return *m_render_target; }
        [[nodiscard]] CommandBuffer& get_current_command_buffer() const;
        [[nodiscard]] std::vector<std::shared_ptr<ImageView>> get_viewport_color_views() const;

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
        Format m_surface_format;
        Format m_depth_format;
        SampleCount m_msaa_samples;
        ClearValue m_color_clear_value;
    };
}
