#pragma once
#include "common/export.h"
#include "graphics/result.h"
#include "graphics/vulkan_settings.h"
#include "render/render_settings.h"
#include "render/quality_settings.h"
#include "render/output_settings.h"
#include "render/scene/render_scene.h"
#include "render/scene/render_geometry.h"
#include "render/scene/scene_resolver.h"
#include "render/scene/scene_picking.h"
#include "render/debug/line_draw_list.h"
#include "render/material/material_renderer.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    class AssetRegistry;
    class Window;
    class CommandBuffer;
    class RenderContext;
    class RenderResources;
    class SceneRenderer;
    class RenderDiagnostics;
    class ImageView;
    class MaterialLayout;
    class MaterialPrograms;
    class FrameScheduler;
    class Presentation;
    class OverlayRecordContext;
    struct SwapchainCompatibility;
    struct QueueSemaphoreSubmit;

    class COMET_API Renderer {
    public:
        class COMET_API MaterialUpdate {
        public:
            MaterialUpdate(MaterialUpdate&&) = default;
            MaterialUpdate& operator=(MaterialUpdate&&) = default;
            void publish() &&;

        private:
            friend class Renderer;
            MaterialUpdate() = default;
            std::vector<MaterialRenderer::MaterialUpdate> m_views;
        };

        struct Settings {
            VulkanSettings vulkan;
            RenderSettings render;
            bool enable_diagnostics = false;
        };

        enum class FramePreparation { Ready, Deferred };
        using ViewId = uint32_t;
        static constexpr ViewId DEFAULT_VIEW = 0;

        struct OffscreenFrame {
            uint32_t slot;
            Math::Vec2u size;
            std::shared_ptr<ImageView> color_view;
        };

        struct Overlay {
            std::function<Result<void, GraphicsError>(OverlayRecordContext&)> render;
            std::function<void()> release;
            std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild;
        };

        static Result<std::unique_ptr<Renderer>, GraphicsError> create(
            const Window& window, const Settings& settings, const AssetRegistry& asset_registry);

        ~Renderer();

        // Ready 才能提取并绘制；Deferred 不持有活动帧。
        [[nodiscard]] Result<FramePreparation, GraphicsError> prepare_frame();
        // 消费场景快照，完成绘制、提交和呈现；帧错误终止本次 Renderer 生命周期。
        [[nodiscard]] Result<void, GraphicsError> render_frame(const RenderScene& render_scene);
        // 无活动场景时只清屏和绘制 UI，不诊断场景相机。
        [[nodiscard]] Result<void, GraphicsError> render_frame();

        Result<void, GraphicsError> enable_offscreen_rendering(Math::Vec2u initial_size);
        // View 在帧边界创建／移除；隐藏只跳过绘制，保留相机与目标。
        [[nodiscard]] Result<ViewId, GraphicsError> create_offscreen_view(Math::Vec2u initial_size);
        [[nodiscard]] Result<void, GraphicsError> remove_offscreen_view(ViewId id);
        [[nodiscard]] OffscreenFrame get_offscreen_frame(ViewId id = DEFAULT_VIEW) const;
        [[nodiscard]] uint32_t max_render_target_dimension() const;
        Result<MaterialRenderer::ReloadReport, GraphicsError> reload_material_shaders(
            MaterialShaders shaders);
        // 编译失败的内置程序在所有视图中显示错误材质，成功发布后恢复。
        void invalidate_material_shaders(std::span<const std::string> programs);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] const MaterialPrograms& get_material_programs() const { return *m_programs; }
        [[nodiscard]] Result<MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<const Material>& material);
        void request_swapchain_recreation();
        // 修改请求在下一次 prepare_frame 的交换链重建中生效。
        void set_vsync_enabled(bool enabled);
        [[nodiscard]] bool is_vsync_enabled() const;
        [[nodiscard]] Result<void, GraphicsError> request_output_settings(OutputSettings settings);
        [[nodiscard]] const OutputSettings& get_output_settings() const { return m_output; }
        [[nodiscard]] bool is_hdr_output() const;
        [[nodiscard]] bool output_pending() const {
            return m_pending_output.has_value() || m_output_recreating;
        }
        [[nodiscard]] Result<QualitySettings, GraphicsError> resolve_quality_settings(
            QualitySettings settings) const;
        [[nodiscard]] Result<void, GraphicsError> request_quality_settings(
            QualitySettings settings);
        [[nodiscard]] const QualitySettings& get_quality_settings() const;
        [[nodiscard]] Math::Vec2u get_scene_size() const;
        [[nodiscard]] std::vector<uint32_t> supported_msaa_samples() const;
        [[nodiscard]] float max_anisotropy() const;
        [[nodiscard]] bool quality_pending() const { return m_pending_quality.has_value(); }
        [[nodiscard]] const std::string& quality_error() const { return m_quality_error; }
        void wait_idle();
        void prepare_shutdown() noexcept;
        // 同一集成方的绘制与交换链生命周期一起安装或解除。
        void set_overlay(Overlay overlay);
        [[nodiscard]] const FrameScheduler& get_frame_scheduler() const { return *m_frames; }
        [[nodiscard]] RenderDiagnostics& get_diagnostics() { return *m_diagnostics; }
        [[nodiscard]] const RenderDiagnostics& get_diagnostics() const { return *m_diagnostics; }

        Result<void, GraphicsError> set_render_view(RenderView view, ViewId id = DEFAULT_VIEW);

        struct ViewportPickResult {
            uint64_t scene_lifetime;
            std::optional<ScenePickHit> hit;
        };
        void request_viewport_pick(Math::Vec2u pixel, Math::Vec2u image_resolution,
            uint64_t scene_lifetime, ViewId id = DEFAULT_VIEW);
        // 帧提交后由宿主取回值；没有结果与命中空白分别表示为两层 optional。
        [[nodiscard]] std::optional<ViewportPickResult> take_viewport_pick_result(
            ViewId id = DEFAULT_VIEW);
        void cancel_viewport_pick(ViewId id = DEFAULT_VIEW);

        // 在场景 pass 录制前追加（update/prepare），仅用于本帧。
        // 没有有效视图时也会消费并清空。
        void submit_lines(const LineDrawList& draw_list, ViewId id = DEFAULT_VIEW);

        [[nodiscard]] RenderResources& get_render_resources() { return *m_render_resources; }
        [[nodiscard]] const RenderResources& get_render_resources() const {
            return *m_render_resources;
        }
        // 仅供诊断读取；生产状态修改经 Renderer 的帧与资源准备接口进入。
        [[nodiscard]] const SceneRenderer& get_scene_renderer(ViewId id = DEFAULT_VIEW) const;
        [[nodiscard]] RenderContext& get_render_context() { return *m_render_context; }
        [[nodiscard]] const RenderContext& get_render_context() const { return *m_render_context; }

    private:
        Renderer(std::unique_ptr<RenderContext> context, std::unique_ptr<RenderResources> resources,
            std::unique_ptr<FrameScheduler> frames, std::unique_ptr<MaterialPrograms> programs,
            std::unique_ptr<SceneRenderer> scene, const AssetRegistry& assets,
            const Settings& settings);
        Result<void, GraphicsError> complete_frame(const RenderScene* render_scene);
        Result<void, GraphicsError> record_overlay(std::vector<QueueSemaphoreSubmit>& waits);
        void discard_frame_requests();
        struct ViewportPickRequest {
            Math::Vec2u pixel;
            Math::Vec2u image_resolution;
            uint64_t scene_lifetime;
        };

        struct ViewState {
            std::unique_ptr<SceneRenderer> scene;
            RenderView view;
            RenderCameraResolver camera;
            std::optional<ViewportPickRequest> pick_request;
            std::optional<ViewportPickResult> pick_result;
            LineDrawList lines;
        };
        [[nodiscard]] ViewState& get_view(ViewId id);
        [[nodiscard]] const ViewState& get_view(ViewId id) const;
        [[nodiscard]] Result<void, GraphicsError> render_view(ViewState& view, bool has_scene,
            std::vector<QueueSemaphoreSubmit>& waits, RenderDiagnostics* diagnostics);

        std::unique_ptr<RenderContext> m_render_context;
        std::unique_ptr<RenderResources> m_render_resources;
        std::unique_ptr<FrameScheduler> m_frames;
        std::unique_ptr<RenderDiagnostics> m_diagnostics;
        std::unique_ptr<Presentation> m_presentation;
        std::unique_ptr<MaterialPrograms> m_programs;
        std::map<ViewId, ViewState> m_views;
        ViewId m_next_view_id = 1;
        Settings m_settings;
        SceneResolver m_scene_resolver;
        const AssetRegistry& m_asset_registry;
        std::function<Result<void, GraphicsError>(OverlayRecordContext&)> m_render_overlay;
        bool m_shutdown_prepared = false;
        RenderWorld m_world;
        RenderGeometry m_geometry;
        std::optional<QualitySettings> m_pending_quality;
        std::string m_quality_error;
        OutputSettings m_output;
        std::optional<OutputSettings> m_pending_output;
        bool m_output_recreating = false;
    };
}
