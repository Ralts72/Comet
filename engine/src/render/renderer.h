#pragma once
#include "common/export.h"
#include "render/scene/render_scene.h"
#include "render/scene/scene_resolver.h"
#include "render/scene/scene_picking.h"
#include "render/line_draw_list.h"

#include <functional>
#include <memory>
#include <optional>

namespace Comet {
    class AssetRegistry;
    class Window;
    class CommandBuffer;
    class RenderContext;
    class ResourceManager;
    class SceneRenderer;
    class Config;

    class COMET_API Renderer {
    public:
        Renderer(const Window& window, const Config& config,
            const AssetRegistry& asset_registry);

        ~Renderer();

        // 成功后才能提取场景并调用 render_frame；准备阶段允许 UI 修改或替换 Scene。
        [[nodiscard]] bool prepare_frame();
        // 消费场景快照，完成绘制、提交和呈现。
        void render_frame(const RenderScene& render_scene);

        void enable_offscreen_rendering(Math::Vec2u initial_size);

        void set_render_view(RenderView view);

        using OverlayPrepareCallback = std::function<void()>;
        using OverlayRenderCallback = std::function<void(CommandBuffer&)>;

        void set_overlay_callbacks(
            OverlayPrepareCallback prepare, OverlayRenderCallback render);

        using ViewportPickCallback = std::function<void(std::optional<ScenePickHit>)>;
        void request_viewport_pick(Math::Vec2u pixel, Math::Vec2u image_resolution);
        void set_viewport_pick_callback(ViewportPickCallback callback);

        // 在场景 pass 录制前追加（update/prepare 或拾取回调），仅用于本帧。
        // 没有有效视图时也会消费并清空。
        void submit_lines(const LineDrawList& draw_list);

        [[nodiscard]] ResourceManager& get_resource_manager() {
            return *m_resource_manager;
        }
        [[nodiscard]] const ResourceManager& get_resource_manager() const {
            return *m_resource_manager;
        }
        [[nodiscard]] SceneRenderer& get_scene_renderer() { return *m_scene_renderer; }
        [[nodiscard]] const SceneRenderer& get_scene_renderer() const {
            return *m_scene_renderer;
        }
        [[nodiscard]] RenderContext& get_render_context() { return *m_render_context; }
        [[nodiscard]] const RenderContext& get_render_context() const {
            return *m_render_context;
        }

    private:
        struct ViewportPickRequest {
            Math::Vec2u pixel;
            Math::Vec2u image_resolution;
        };

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<ResourceManager> m_resource_manager;
        std::unique_ptr<SceneRenderer> m_scene_renderer;
        SceneResolver m_scene_resolver;
        RenderView m_render_view;
        OverlayPrepareCallback m_prepare_overlay;
        OverlayRenderCallback m_render_overlay;
        std::optional<ViewportPickRequest> m_viewport_pick_request;
        ViewportPickCallback m_viewport_pick_callback;
        LineDrawList m_line_draw_list;
    };
}
