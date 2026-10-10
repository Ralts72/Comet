#include "project/game_ui.h"

#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/renderer.h"

#include <utility>

namespace CometEditor {
    GameUi::GameUi(Comet::Engine& engine, const Comet::Project& project,
        Comet::Ui::ProjectUi::Services services)
        : m_engine(engine), m_project(project), m_services(std::move(services)) {
        reload();
    }

    void GameUi::reload() {
        if(m_ui) {
            if(auto result = m_ui->reload(); !result)
                LOG_WARN("Cannot reload project UI: {}", result.error());
            return;
        }
        if(!m_project.ui())
            return;
        m_services.input_actions = m_project.input_actions();
        m_services.display_defaults = m_project.display_settings();
        m_services.quality_defaults = m_project.quality_settings();
        auto created = Comet::Ui::ProjectUi::create(m_engine.get_window(), m_engine.get_renderer(),
            *m_project.ui(), {.resource_root = m_project.paths().assets()}, m_services);
        if(!created) {
            LOG_WARN("Project UI unavailable: {}; use Reload UI after fixing the source",
                created.error().message);
            return;
        }
        m_ui = std::move(created).value();
    }

    void GameUi::reset() {
        deactivate();
        m_engine.get_renderer().wait_idle();
        m_ui.reset();
        m_render = false;
        reload();
    }

    void GameUi::deactivate() {
        if(m_ui)
            m_ui->deactivate();
        m_render = false;
    }

    bool GameUi::is_modal() const {
        return m_render && m_ui && m_ui->is_modal();
    }

    Comet::Result<Comet::Ui::ProjectUi::FrameResult, Comet::Error> GameUi::frame(
        const Comet::Input::Frame& input, Comet::Ui::ProjectUi::FrameInfo info) {
        using Result = Comet::Result<Comet::Ui::ProjectUi::FrameResult, Comet::Error>;
        if(!m_ui || !info.view) {
            if(m_render)
                deactivate();
            m_render = false;
            return Result::success({});
        }
        m_render = true;
        return m_ui->frame(input, std::move(info));
    }

    Comet::Result<void, Comet::GraphicsError> GameUi::render(Comet::OverlayRecordContext& frame) {
        if(m_render && m_ui)
            return m_ui->render(frame, Comet::Ui::RenderOutput::Offscreen);
        return Comet::Result<void, Comet::GraphicsError>::success();
    }

    void GameUi::release() {
        if(m_ui)
            m_ui->release_swapchain_resources();
    }

    Comet::Result<void, Comet::GraphicsError> GameUi::rebuild(
        const Comet::SwapchainCompatibility& compatibility) {
        if(m_ui)
            return m_ui->rebuild_swapchain_resources(compatibility);
        return Comet::Result<void, Comet::GraphicsError>::success();
    }
}
