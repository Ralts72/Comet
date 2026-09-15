#pragma once

#include "config/config.h"
#include "common/export.h"
#include "common/retry_backoff.h"
#include "graphics/queue.h"
#include "render/material_renderer.h"
#include "render/scene/render_submission.h"
#include "render/line_draw_list.h"

#include <chrono>
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Device;
    class ResourceManager;
    class FrameScheduler;
    class RenderTarget;
    class ImageView;
    class Swapchain;
    struct SwapchainCompatibility;

    class COMET_API SceneRenderer {
    public:
        SceneRenderer(Device& device, Format surface_format, const Config::Vulkan& vulkan,
            const Config::Render& render);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] const MaterialRenderer::Statistics& get_material_statistics() const;
        [[nodiscard]] RenderTarget& get_render_target();
        [[nodiscard]] const RenderTarget& get_render_target() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_offscreen_color_view(uint32_t slot) const;

        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render_scene_pass(
            FrameScheduler& frames, const RenderSubmission& submission,
            const LineDrawList& lines = {});
        // 普通失败保留旧目标并管理重试；设备错误终止调用链。
        Result<void, GraphicsError> resize_offscreen_target(Math::Vec2u size,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    private:
        friend class Renderer;
        Result<void, GraphicsError> configure_presentation(
            ResourceManager& resources, Swapchain& swapchain);
        Result<void, GraphicsError> configure_offscreen(
            ResourceManager& resources, Math::Vec2u size);
        Result<MaterialRenderer::ReloadReport, GraphicsError> reload_material_shaders(
            MaterialRenderer::ShaderCode shaders);
        void release_presentation_target();
        Result<void, GraphicsError> rebuild_presentation_target(
            Swapchain& swapchain, const SwapchainCompatibility& compatibility);

        struct TargetState;
        struct ResizeFailure {
            Math::Vec2u size;
            RetryBackoff retry;
        };
        Result<std::shared_ptr<TargetState>, GraphicsError> create_target(
            ResourceManager& resources, Swapchain* swapchain, Math::Vec2u size);

        Device& m_device;
        Format m_surface_format;
        Format m_depth_format;
        SampleCount m_msaa_samples;
        Math::Vec4 m_clear_color;
        uint32_t m_frame_slot_count;
        std::shared_ptr<TargetState> m_target;
        std::optional<ResizeFailure> m_resize_failure;
        std::optional<MaterialRenderer::ShaderCode> m_material_shaders;
    };
}
