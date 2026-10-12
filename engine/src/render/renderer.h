#pragma once
#include "common/export.h"
#include "asset/registry.h"
#include "graphics/result.h"
#include "graphics/vulkan_settings.h"
#include "render/render_settings.h"
#include "render/render_frame.h"
#include "render/quality_settings.h"
#include "render/output_settings.h"
#include "render/scene/render_scene.h"
#include "render/scene/render_geometry.h"
#include "render/scene/scene_resolver.h"
#include "render/scene/scene_picking.h"
#include "render/debug/line_draw_list.h"
#include "render/material/material_renderer.h"
#include "render/material/material_programs.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Comet {
    class Window;
    class Engine;
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
    class RenderThread;
    class OverlayRecordContext;
    struct SwapchainCompatibility;
    struct QueueSemaphoreSubmit;

    // Main 在帧边界修改视图和读取反馈；GPU 资产、材质及 Shader 发布经 RenderThread 执行。
    // submit 到 complete 之间，宿主不能访问这里的可变状态，包括诊断快照。
    class COMET_API Renderer {
    public:
        class COMET_API MaterialUpdate {
        public:
            MaterialUpdate(MaterialUpdate&&) = default;
            MaterialUpdate& operator=(MaterialUpdate&&) = default;
            [[nodiscard]] Result<void, GraphicsError> publish() &&;

        private:
            friend class Renderer;
            MaterialUpdate() = default;
            Renderer* m_owner = nullptr;
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
            std::function<OverlayDrawList()> capture;
            std::function<void()> release;
            std::function<Result<void, GraphicsError>(const SwapchainCompatibility&)> rebuild;
            std::function<void()> shutdown;
        };

        static Result<std::unique_ptr<Renderer>, GraphicsError> create(
            const Window& window, const Settings& settings);

        ~Renderer();

        // Main 捕获窗口尺寸；GPU 侧准备完成且 Ready 才能捕获帧。
        [[nodiscard]] Result<FramePreparation, GraphicsError> prepare_frame(
            AssetRegistry::Removals removals = {});
        [[nodiscard]] RenderFrame capture_frame();
        // 同步消费已解析的世界；Engine 使用捕获的帧交给 RenderThread。
        [[nodiscard]] Result<void, GraphicsError> render_frame(
            std::shared_ptr<const RenderWorld> world = {});

        Result<void, GraphicsError> enable_offscreen_rendering(Math::Vec2u initial_size);
        // View 在帧边界创建／移除；隐藏只跳过绘制，保留相机与目标。
        [[nodiscard]] Result<ViewId, GraphicsError> create_offscreen_view(Math::Vec2u initial_size);
        [[nodiscard]] Result<void, GraphicsError> remove_offscreen_view(ViewId id);
        [[nodiscard]] OffscreenFrame get_offscreen_frame(ViewId id = DEFAULT_VIEW) const;
        [[nodiscard]] uint32_t max_render_target_dimension() const;
        Result<MaterialRenderer::ReloadReport, GraphicsError> reload_material_shaders(
            MaterialShaders shaders);
        // 编译失败的内置程序在所有视图中显示错误材质，成功发布后恢复。
        [[nodiscard]] Result<void, GraphicsError> invalidate_material_shaders(
            std::span<const std::string> programs);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] MaterialPrograms::Feedback get_material_program_feedback() const {
            return m_programs->feedback();
        }
        // Main 先捕获材质快照和 Shader 版本；GPU 准备只消费这次输入。
        [[nodiscard]] Result<MaterialUpdate, GraphicsError> prepare_material_update(
            AssetHandle handle, const std::shared_ptr<const Material>& snapshot,
            const std::shared_ptr<const ShaderProgramArtifact>& program_source);
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
        // Main 捕获绘制对象；同一集成方的交换链生命周期一起安装或解除。
        void set_overlay(Overlay overlay);
        // UI 后端在帧交接边界同步准备或清理 GPU 资源；回调不能访问窗口、输入或布局。
        [[nodiscard]] Result<void, GraphicsError> prepare_overlay_resources(
            const std::function<Result<void, GraphicsError>()>& prepare);
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
        friend class Engine;
        friend class RenderThread;
        [[nodiscard]] Result<void, GraphicsError> begin_render_loop();
        void end_render_loop();
        void collect_resources();
        [[nodiscard]] Result<FramePreparation, GraphicsError> prepare_gpu_frame(
            Math::Vec2u framebuffer_size, AssetRegistry::Removals removals);
        [[nodiscard]] Result<void, GraphicsError> record_frame(RenderFrame frame);
        [[nodiscard]] Result<void, GraphicsError> draw_frame(RenderFrame& frame);
        Renderer(const Window& window, std::unique_ptr<RenderContext> context,
            std::unique_ptr<RenderResources> resources, std::unique_ptr<FrameScheduler> frames,
            std::unique_ptr<MaterialPrograms> programs, std::unique_ptr<SceneRenderer> scene,
            const Settings& settings);
        Result<void, GraphicsError> record_overlay(
            const OverlayDrawList& draws, std::vector<QueueSemaphoreSubmit>& waits);
        void discard_frame_requests();

        struct ViewState {
            std::unique_ptr<SceneRenderer> scene;
            RenderView view;
            RenderCameraResolver camera;
            std::optional<RenderFrame::Pick> pick_request;
            std::optional<ViewportPickResult> pick_result;
            LineDrawList lines;
        };
        [[nodiscard]] ViewState& get_view(ViewId id);
        [[nodiscard]] const ViewState& get_view(ViewId id) const;
        [[nodiscard]] Result<void, GraphicsError> render_view(ViewState& view,
            const RenderFrame::View& input, const RenderWorld& world, bool has_scene,
            std::vector<QueueSemaphoreSubmit>& waits, RenderDiagnostics* diagnostics);

        const Window& m_window;
        std::unique_ptr<RenderContext> m_render_context;
        RenderThread* m_render_thread = nullptr; // Engine 绑定，排空请求后解除。
        std::unique_ptr<RenderResources> m_render_resources;
        std::unique_ptr<FrameScheduler> m_frames;
        std::unique_ptr<RenderDiagnostics> m_diagnostics;
        std::unique_ptr<Presentation> m_presentation;
        std::unique_ptr<MaterialPrograms> m_programs;
        std::map<ViewId, ViewState> m_views;
        ViewId m_next_view_id = 1;
        Settings m_settings;
        std::function<OverlayDrawList()> m_capture_overlay;
        std::function<void()> m_shutdown_overlay;
        bool m_shutdown_prepared = false;
        double m_frame_preparation_ms = 0;
        std::shared_ptr<const RenderWorld> m_world;
        RenderGeometry m_geometry;
        std::optional<QualitySettings> m_pending_quality;
        std::string m_quality_error;
        OutputSettings m_output;
        std::optional<OutputSettings> m_pending_output;
        bool m_output_recreating = false;
    };
}
