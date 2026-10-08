#pragma once

#include "ui/project_ui.h"

namespace Comet {
    class Engine;
}

namespace CometEditor {
    // Editor 宿主适配；页面与交互行为仍由项目资源和控制器定义。
    class GameUi final {
    public:
        GameUi(Comet::Engine& engine, const Comet::Project& project,
            Comet::Ui::ProjectUi::Services services);
        void reload();
        void reset();
        void deactivate();
        [[nodiscard]] bool is_modal() const;
        [[nodiscard]] Comet::Result<Comet::Ui::ProjectUi::FrameResult, Comet::Error> frame(
            const Comet::Input::Frame& input, Comet::Ui::ProjectUi::FrameInfo info);
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> render(
            Comet::OverlayRecordContext& frame);
        void release();
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> rebuild(
            const Comet::SwapchainCompatibility& compatibility);

    private:
        Comet::Engine& m_engine;
        const Comet::Project& m_project;
        Comet::Ui::ProjectUi::Services m_services;
        std::unique_ptr<Comet::Ui::ProjectUi> m_ui;
        bool m_render = false;
    };
}
