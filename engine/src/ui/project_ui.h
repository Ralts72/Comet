#pragma once

#include "common/export.h"

#include "core/project.h"
#include "input/input_overrides.h"
#include "ui/rml_context.h"

namespace Comet::Ui {
    // 项目呈现生命周期独立于 Scene；宿主只提供持久化和 Runtime 应用服务。
    class COMET_API ProjectUi final {
    public:
        struct Services {
            InputActions input_actions;
            std::function<Result<InputOverrides>()> load_input;
            std::function<Result<void>(InputOverrides)> apply_input;
            DisplaySettings display_defaults;
            std::function<Result<DisplaySettings>()> load_display;
            std::function<Result<void>(DisplaySettings)> apply_display;
            std::function<std::optional<float>()> display_confirmation;
            std::function<Result<void>()> confirm_display;
            std::function<Result<void>()> revert_display;
            QualitySettings quality_defaults;
            std::function<Result<QualitySettings>()> load_quality;
            std::function<Result<void>(QualitySettings)> apply_quality;
            AudioSettings audio_defaults;
            std::function<Result<AudioSettings>()> load_audio;
            std::function<AudioSettings()> active_audio;
            std::function<Result<void>(AudioSettings)> apply_audio;
        };
        struct FrameInfo {
            float fps = 0;
            bool game_available = true;
            std::optional<View> view;
            bool display_preview = false;
            std::optional<bool> vsync_active;
        };
        struct FrameResult {
            bool blocked = false;
            bool pointer_blocked = false;
        };

        static Result<std::unique_ptr<ProjectUi>, Error> create(Window& window, Renderer& renderer,
            const Project::UiEntry& entry, RmlContext::Options options, Services services = {});
        ~ProjectUi();
        ProjectUi(const ProjectUi&) = delete;
        ProjectUi& operator=(const ProjectUi&) = delete;

        [[nodiscard]] Result<FrameResult, Error> frame(const Input::Frame& input, FrameInfo info);
        [[nodiscard]] bool is_modal() const;
        [[nodiscard]] Result<void> reload();
        // 当前已发布页面的静态依赖；成功重载后更新。
        [[nodiscard]] const std::vector<std::filesystem::path>& resource_dependencies() const;
        // 场景重启等宿主状态变更；页面仍由项目控制器决定如何呈现。
        void deactivate();
        [[nodiscard]] Result<void, GraphicsError> render(
            OverlayRecordContext& frame, RenderOutput output = RenderOutput::Presentation);
        void release_swapchain_resources();
        [[nodiscard]] Result<void, GraphicsError> rebuild_swapchain_resources(
            const SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit ProjectUi(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}
