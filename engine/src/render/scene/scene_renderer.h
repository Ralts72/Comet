#pragma once

#include "graphics/vulkan_settings.h"
#include "render/render_settings.h"
#include "render/quality_settings.h"
#include "common/export.h"
#include "common/retry_backoff.h"
#include "graphics/queue.h"
#include "render/material/material_renderer.h"
#include "render/passes/shadow_pass.h"
#include "render/passes/path_trace_pass.h"
#include "render/scene/render_submission.h"
#include "render/scene/render_geometry.h"
#include "render/debug/line_draw_list.h"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

namespace Comet {
    class Device;
    class AssetRegistry;
    class MaterialPrograms;
    class RenderResources;
    class FrameScheduler;
    class RenderDiagnostics;
    class CommandBuffer;
    class RenderTarget;
    class ImageView;
    class Swapchain;
    class BloomPass;
    class VolumetricFogPass;
    class ScreenSpaceReflectionPass;
    struct SwapchainCompatibility;

    class COMET_API SceneRenderer {
    public:
        // 显式创建指定尺寸的离屏输出。
        static Result<std::unique_ptr<SceneRenderer>, GraphicsError> create(Device& device,
            MaterialPrograms& programs, RenderResources& resources, const VulkanSettings& vulkan,
            const RenderSettings& render, Math::Vec2u size);
        // 按 scene_output 选择输出；离屏模式以交换链尺寸初始化。
        static Result<std::unique_ptr<SceneRenderer>, GraphicsError> create(Device& device,
            MaterialPrograms& programs, RenderResources& resources, const VulkanSettings& vulkan,
            const RenderSettings& render, Swapchain& swapchain);
        [[nodiscard]] std::vector<std::shared_ptr<const MaterialLayout>> get_material_layouts()
            const;
        [[nodiscard]] MaterialRenderer::Statistics get_material_statistics() const;
        [[nodiscard]] ShadowPass::Statistics get_shadow_statistics() const;
        [[nodiscard]] PathTracePass::Statistics get_path_trace_statistics() const;
        [[nodiscard]] const PostProcessSettings& get_post_process_settings() const;
        [[nodiscard]] const QualitySettings& get_quality_settings() const { return m_quality; }
        [[nodiscard]] Math::Vec2u get_scene_size() const;
        [[nodiscard]] RenderTarget& get_render_target();
        [[nodiscard]] const RenderTarget& get_render_target() const;
        [[nodiscard]] bool is_offscreen() const;
        [[nodiscard]] std::shared_ptr<ImageView> get_offscreen_color_view(uint32_t slot) const;
        [[nodiscard]] std::shared_ptr<ImageView> get_hdr_color_view(uint32_t slot) const;

        // 在录制前准备；OOM 保留上一份可用设置，并进行有界重试。
        [[nodiscard]] Result<void, GraphicsError> prepare_post_process(
            const PostProcessSettings& settings,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
        [[nodiscard]] Result<void, GraphicsError> prepare_material_programs(
            const RenderWorld& world);
        // 宿主加载期预热已发布程序的实际材质组合，不在稳定帧重复遍历。
        [[nodiscard]] Result<void, GraphicsError> warmup_scene(const RenderWorld& world);
        // 仅录制已准备的状态；场景快照的后处理设置由上一步消费。
        [[nodiscard]] Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> render(
            FrameScheduler& frames, const RenderSubmission& submission,
            const RenderGeometry& geometry, const LineDrawList& lines = {},
            RenderDiagnostics* diagnostics = nullptr);
        // 普通失败保留旧目标并管理重试；设备错误终止调用链。
        Result<void, GraphicsError> resize_offscreen_target(Math::Vec2u size,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    private:
        friend class Renderer;
        friend class ScenePreview;
        struct RenderState;
        SceneRenderer(Device& device, MaterialPrograms& programs, const VulkanSettings& vulkan,
            const RenderSettings& render);
        void skip_frame();
        MaterialRenderer& get_material_renderer();
        void remove_assets(const AssetRegistry::Removals& removals);
        struct ViewRequirements {
            bool shadows = false;
            bool soft_particles = false;
            bool decals = false;
            bool fog = false;
            [[nodiscard]] bool needs_depth(
                const PostProcessSettings& settings, bool temporal) const {
                return settings.uses_ao() || temporal || soft_particles || decals;
            }
            bool operator==(const ViewRequirements&) const = default;
        };
        Result<void, GraphicsError> configure_effects(
            const PostProcessSettings& settings, const ViewRequirements& requirements);
        Result<void, GraphicsError> validate_quality_settings(
            const QualitySettings& settings) const;
        Result<void, GraphicsError> configure_output_calibration(float headroom, float white_level);
        Result<void, GraphicsError> configure_presentation(
            RenderResources& resources, Swapchain& swapchain);
        Result<void, GraphicsError> configure_offscreen(
            RenderResources& resources, Math::Vec2u size);
        Result<MaterialRenderer::ShaderUpdate, GraphicsError> prepare_material_shaders(
            const MaterialShaders& shaders);
        void release_presentation_target();
        Result<void, GraphicsError> rebuild_presentation_target(RenderResources& resources,
            Swapchain& swapchain, const SwapchainCompatibility& compatibility);

        Result<std::shared_ptr<RenderState>, GraphicsError> prepare_quality(
            RenderResources& resources, Swapchain& swapchain, const QualitySettings& settings);
        void install_quality(std::shared_ptr<RenderState> state, const QualitySettings& settings);
        struct EffectPasses {
            BloomPass* bloom;
            VolumetricFogPass* fog;
            ScreenSpaceReflectionPass* reflections;
        };
        Result<void, GraphicsError> rebuild_graph(RenderState& state,
            const PostProcessSettings& settings, const ViewRequirements& requirements,
            EffectPasses effects, Math::Vec2u source_size);
        struct TargetRetry {
            Math::Vec2u size;
            RetryBackoff retry;
        };
        Result<std::shared_ptr<RenderState>, GraphicsError> create_state(RenderResources& resources,
            Swapchain* swapchain, Math::Vec2u size, const QualitySettings& quality);
        Result<void, GraphicsError> replace_targets(
            RenderState& state, Swapchain* swapchain, Math::Vec2u size);
        Result<void, GraphicsError> record_pass(std::size_t pass, FrameScheduler& frames,
            const RenderSubmission& submission, const RenderGeometry& geometry,
            const RenderGeometry& main_geometry, const LineDrawList& lines,
            const LightingData& lighting, std::vector<QueueSemaphoreSubmit>& waits);
        Result<std::vector<QueueSemaphoreSubmit>, GraphicsError> draw_scene(FrameScheduler& frames,
            CommandBuffer& command, const RenderSubmission& submission, const LineDrawList& lines);
        void record_transparent(FrameScheduler& frames, std::vector<QueueSemaphoreSubmit>& waits);

        Device& m_device;
        MaterialPrograms& m_programs;
        Format m_offscreen_format;
        float m_hdr_headroom;
        float m_hdr_white_level;
        Format m_depth_format;
        QualitySettings m_quality;
        uint32_t m_frame_slot_count;
        PostProcessSettings m_post_process;
        std::shared_ptr<ShadowPass> m_shadow;
        std::shared_ptr<RenderState> m_state;
        std::optional<TargetRetry> m_resize_failure;
        std::optional<TargetRetry> m_post_process_failure;
    };
}
