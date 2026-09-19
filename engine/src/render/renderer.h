#pragma once
#include "common/export.h"
#include "graphics/result.h"
#include "render/scene/render_scene.h"
#include "render/scene/scene_resolver.h"
#include "render/scene/scene_picking.h"
#include "render/debug/line_draw_list.h"
#include "render/frame_scheduler.h"
#include "render/material/material_renderer.h"
#include "render/presentation.h"

#include <functional>
#include <memory>
#include <optional>

namespace Comet {
    class AssetRegistry;
    class Window;
    class CommandBuffer;
    class RenderContext;
    class RenderResources;
    class SceneRenderer;
    class Config;

    class COMET_API Renderer {
    public:
        static Result<std::unique_ptr<Renderer>, GraphicsError> create(
            const Window& window, const Config& config, const AssetRegistry& asset_registry);

        ~Renderer();

        // 成功值 true 才能提取并绘制；false 表示延期，准备阶段允许 UI 修改或替换 Scene。
        [[nodiscard]] Result<bool, GraphicsError> prepare_frame();
        // 消费场景快照，完成绘制、提交和呈现。
        [[nodiscard]] Result<void, GraphicsError> render_frame(const RenderScene& render_scene);

        Result<void, GraphicsError> enable_offscreen_rendering(Math::Vec2u initial_size);
        Result<MaterialRenderer::ReloadReport, GraphicsError> reload_material_shaders(
            MaterialRenderer::ShaderCode shaders);
        void request_swapchain_recreation();
        void wait_idle();
        void prepare_shutdown() noexcept;
        void set_swapchain_resource_callbacks(std::function<void()> release,
            std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild);
        [[nodiscard]] const FrameScheduler& get_frame_scheduler() const { return *m_frames; }

        Result<void, GraphicsError> set_render_view(RenderView view);

        using OverlayRenderCallback = std::function<void(CommandBuffer&)>;

        void set_overlay_renderer(OverlayRenderCallback render);

        using ViewportPickCallback = std::function<void(std::optional<ScenePickHit>)>;
        void request_viewport_pick(Math::Vec2u pixel, Math::Vec2u image_resolution);
        void set_viewport_pick_callback(ViewportPickCallback callback);

        // 在场景 pass 录制前追加（update/prepare 或拾取回调），仅用于本帧。
        // 没有有效视图时也会消费并清空。
        void submit_lines(const LineDrawList& draw_list);

        [[nodiscard]] RenderResources& get_render_resources() { return *m_render_resources; }
        [[nodiscard]] const RenderResources& get_render_resources() const {
            return *m_render_resources;
        }
        [[nodiscard]] SceneRenderer& get_scene_renderer() { return *m_scene_renderer; }
        [[nodiscard]] const SceneRenderer& get_scene_renderer() const { return *m_scene_renderer; }
        [[nodiscard]] RenderContext& get_render_context() { return *m_render_context; }
        [[nodiscard]] const RenderContext& get_render_context() const { return *m_render_context; }

    private:
        Renderer(std::unique_ptr<RenderContext> context, std::unique_ptr<RenderResources> resources,
            std::unique_ptr<FrameScheduler> frames, std::unique_ptr<SceneRenderer> scene,
            const AssetRegistry& assets);
        struct ViewportPickRequest {
            Math::Vec2u pixel;
            Math::Vec2u image_resolution;
        };

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<RenderResources> m_render_resources;
        std::unique_ptr<FrameScheduler> m_frames;
        std::unique_ptr<Presentation> m_presentation;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        SceneResolver m_scene_resolver;
        RenderView m_render_view;
        OverlayRenderCallback m_render_overlay;
        bool m_shutdown_prepared = false;
        std::optional<ViewportPickRequest> m_viewport_pick_request;
        ViewportPickCallback m_viewport_pick_callback;
        LineDrawList m_line_draw_list;
    };
}
