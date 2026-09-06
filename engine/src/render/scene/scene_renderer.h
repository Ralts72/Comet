#pragma once
#include "config/config.h"
#include "common/export.h"
#include "core/math_utils.h"
#include "render/frame_scheduler.h"
#include "graphics/queue.h"
#include "graphics/enums.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/render_pass.h"
#include "graphics/vk_common.h"
#include "render/render_context.h"
#include "render/scene/render_submission.h"
#include "render/render_target.h"
#include "render/debug/debug_renderer.h"
#include "render/material_renderer.h"

#include <functional>
#include <memory>
#include <span>
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
        MaterialRenderer::ReloadReport reload_material_shaders(
            ResourceManager& resources, const ShaderManager::Bytecodes& bytecodes);
        bool reload_debug_shaders(
            ResourceManager& resources, const ShaderManager::Bytecodes& bytecodes);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>>
        get_material_layouts() const;

        [[nodiscard]] std::vector<QueueSemaphoreSubmit> render_scene_pass(
            const RenderSubmission& submission, const LineDrawList& lines = {});

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
        [[nodiscard]] const MaterialRenderer::Statistics& get_material_statistics()
            const {
            return m_material_renderer->get_statistics();
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
        void reset_render_pipeline();
        void set_render_target_clear_color() const;

        SwapchainReleaseCallback m_release_swapchain_resources;
        SwapchainRebuildCallback m_rebuild_swapchain_resources;
        RenderContext& m_context;
        std::shared_ptr<RenderPass> m_render_pass;
        std::unique_ptr<PipelineManager> m_pipeline_manager;
        std::unique_ptr<FrameScheduler> m_frame_scheduler;
        std::shared_ptr<RenderTarget> m_render_target;
        bool m_uses_offscreen_target = false;
        std::unique_ptr<MaterialRenderer> m_material_renderer;
        std::unique_ptr<DebugRenderer> m_debug_renderer;
        Format m_surface_format;
        Format m_depth_format;
        SampleCount m_msaa_samples;
        ClearValue m_color_clear_value;
    };
}
