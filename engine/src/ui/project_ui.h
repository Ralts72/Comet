#pragma once

#include "core/project.h"
#include "input/input_overrides.h"
#include "ui/rml_context.h"

namespace Comet::Ui {
    // 项目呈现生命周期独立于 Scene；宿主只提供持久化和 Runtime 应用服务。
    class ProjectUi final {
    public:
        struct Services {
            InputActions input_actions;
            std::function<Result<InputOverrides>()> load_input;
            std::function<Result<void>(InputOverrides)> apply_input;
        };
        struct FrameInfo {
            float fps = 0;
            bool game_available = true;
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
        // 场景重启等宿主状态变更；页面仍由项目控制器决定如何呈现。
        void deactivate();
        [[nodiscard]] Result<void, GraphicsError> render(OverlayRecordContext& frame);
        void release_swapchain_resources();
        [[nodiscard]] Result<void, GraphicsError> rebuild_swapchain_resources(
            const SwapchainCompatibility& compatibility);

    private:
        class Impl;
        explicit ProjectUi(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };
}
