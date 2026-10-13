#include "project/game_ui.h"

#include "diagnostics/logger.h"
#include "render/renderer.h"

#include <utility>

namespace CometEditor {
    GameUi::GameUi(Comet::Window& window, Comet::Renderer& renderer, const Comet::Project& project,
        Comet::Ui::ProjectUi::Services services)
        : m_window(window), m_renderer(renderer), m_project(project),
          m_services(std::move(services)) {
        reload();
    }

    void GameUi::reload() {
        if(m_ui) {
            if(auto result = m_ui->reload(); !result) {
                m_error = result.error();
                LOG_WARN("Cannot reload project UI: {}", m_error);
            } else {
                m_suspended = false;
                m_error.clear();
            }
            return;
        }
        if(!m_project.ui()) {
            m_error.clear();
            m_suspended = false;
            return;
        }
        m_services.input_actions = m_project.input_actions();
        m_services.display_defaults = m_project.display_settings();
        m_services.quality_defaults = m_project.quality_settings();
        m_services.audio_defaults = m_project.audio_settings();
        auto created = Comet::Ui::ProjectUi::create(m_window, m_renderer, *m_project.ui(),
            {.resource_root = m_project.paths().assets()}, m_services);
        if(!created) {
            m_error = created.error().message;
            LOG_WARN("Project UI unavailable: {}; use Reload UI after fixing the source", m_error);
            return;
        }
        m_ui = std::move(created).value();
        m_suspended = false;
        m_error.clear();
    }

    void GameUi::reset() {
        deactivate();
        if(m_ui) {
            const auto cleared = m_renderer.prepare_overlay_resources([this] {
                m_renderer.wait_idle();
                m_ui->shutdown_gpu();
                return Comet::Result<void, Comet::GraphicsError>::success();
            });
            if(!cleared) {
                LOG_ERROR("Cannot reset project UI: {}", cleared.error().message);
                return;
            }
        }
        m_ui.reset();
        m_error.clear();
        m_suspended = false;
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
        if(m_suspended || !m_ui || !info.view) {
            if(m_render)
                deactivate();
            return Result::success({});
        }
        m_render = true;
        const auto result = m_ui->frame(input, std::move(info));
        if(result)
            return Result::success(result.value());
        if(!Comet::Ui::is_controller_error(result.error()))
            return Result::failure(result.error());
        m_error = result.error().message;
        LOG_ERROR("Project UI stopped: {}; use Reload UI after fixing the controller", m_error);
        m_suspended = true;
        deactivate();
        return Result::success({.blocked = true, .pointer_blocked = true});
    }

    std::unique_ptr<Comet::OverlayDraw> GameUi::take_draw_frame() {
        if(m_render && m_ui)
            return m_ui->take_draw_frame(Comet::Ui::RenderOutput::Offscreen);
        return {};
    }

    void GameUi::release() {
        if(m_ui)
            m_ui->release_swapchain_resources();
    }
    void GameUi::shutdown_gpu() {
        if(m_ui)
            m_ui->shutdown_gpu();
    }

    Comet::Result<void, Comet::GraphicsError> GameUi::rebuild(
        const Comet::SwapchainCompatibility& compatibility) {
        if(m_ui)
            return m_ui->rebuild_swapchain_resources(compatibility);
        return Comet::Result<void, Comet::GraphicsError>::success();
    }
}
