#pragma once
#include "config/config.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "render/frame_scheduler.h"
#include "graphics/queue.h"
#include "graphics/resource/buffer.h"
#include "graphics/pipeline/descriptor_set.h"
#include "graphics/enums.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/resource/sampler.h"
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

    class COMET_API SceneRenderer {
    public:
        SceneRenderer(RenderContext& context, const Config::Vulkan& vulkan_config,
            const Config::Render& render_config);

        void setup_render_pass();

        void setup_offscreen_render_pass(Math::Vec2u size);

        void setup_pipeline(ResourceManager& resource_manager);

        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render_scene_pass(
            const RenderSubmission& submission);

        [[nodiscard]] bool begin_frame();

        void end_frame(std::span<const QueueSemaphoreSubmit> resource_waits);

        void resize_offscreen_target(Math::Vec2u size);

        [[nodiscard]] FrameScheduler& get_frame_scheduler() { return *m_frame_scheduler; }
        [[nodiscard]] const FrameScheduler& get_frame_scheduler() const {
            return *m_frame_scheduler;
        }
        [[nodiscard]] RenderTarget& get_render_target() { return *m_render_target; }
        [[nodiscard]] const RenderTarget& get_render_target() const {
            return *m_render_target;
        }
        [[nodiscard]] CommandBuffer& get_current_command_buffer() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_offscreen_color_view(
            uint32_t frame_slot_index) const;

        [[nodiscard]] bool recreate_swapchain();

        using SwapchainReleaseCallback = std::function<void()>;
        using SwapchainRebuildCallback =
            std::function<void(const SwapchainCompatibility&)>;
        void set_swapchain_resource_callbacks(SwapchainReleaseCallback release_resources,
            SwapchainRebuildCallback rebuild_resources);

    private:
        struct DescriptorResources {
            std::shared_ptr<Buffer> view_project_buffer;
            std::array<std::shared_ptr<Texture>, 2> textures;
        };

        struct MaterialDescriptorState {
            std::shared_ptr<DescriptorPool> pool;
            std::vector<DescriptorSet> descriptor_sets;
            std::vector<DescriptorResources> resources;
            uint64_t last_used_frame_serial = 0;
        };

        [[nodiscard]] const DescriptorSet& prepare_material_descriptor_set(
            const MaterialBinding& material,
            const std::shared_ptr<Buffer>& view_project_buffer, const Sampler& sampler);

        std::shared_ptr<DescriptorSetLayout> create_descriptor_set_layout(
            const DescriptorSetLayoutBindings& bindings);

        void render_item(const ResolvedRenderItem& render_item,
            const DescriptorSet& descriptor_set) const;

        void update_descriptor_set(const DescriptorSet& descriptor_set,
            const DescriptorResources& resources, const Sampler& sampler) const;

        void reset_render_pipeline();
        void collect_completed_material_descriptors();
        void set_render_target_clear_color() const;

        SwapchainReleaseCallback m_release_swapchain_resources;
        SwapchainRebuildCallback m_rebuild_swapchain_resources;
        RenderContext& m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameScheduler> m_frame_scheduler;
        std::shared_ptr<RenderTarget> m_render_target;
        bool m_uses_offscreen_target = false;
        std::shared_ptr<Pipeline> m_pipeline;
        std::shared_ptr<Sampler> m_default_sampler;
        std::shared_ptr<DescriptorSetLayout> m_descriptor_set_layout;
        std::unordered_map<AssetHandle, MaterialDescriptorState> m_material_descriptors;
        std::vector<std::shared_ptr<Buffer>> m_view_project_uniform_buffers;
        Format m_surface_format;
        Format m_depth_format;
        SampleCount m_msaa_samples;
        ClearValue m_color_clear_value;
    };
}
