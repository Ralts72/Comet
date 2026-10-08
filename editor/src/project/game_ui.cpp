#include "project/game_ui.h"

#include "core/engine.h"
#include "diagnostics/logger.h"
#include "render/renderer.h"

namespace CometEditor {
    GameUi::GameUi(Comet::Engine& engine, const Comet::Project& project)
        : m_engine(engine), m_project(project) {
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
        auto created = Comet::Ui::ProjectUi::create(m_engine.get_window(), m_engine.get_renderer(),
            *m_project.ui(), {.resource_root = m_project.paths().assets()},
            {.input_actions = m_project.input_actions(),
                .load_input = [this]() -> Comet::Result<Comet::InputOverrides> {
                    auto loaded = Comet::PlayerInputSettings::load(m_project.id());
                    if(!loaded)
                        return Comet::Result<Comet::InputOverrides>::failure(loaded.error());
                    m_settings = std::move(loaded).value();
                    return Comet::Result<Comet::InputOverrides>::success(m_settings->overrides());
                },
                .apply_input =
                    [this](Comet::InputOverrides overrides) {
                        return apply_input(std::move(overrides));
                    }});
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
        m_settings.reset();
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
        auto result = m_ui->frame(input, std::move(info));
        if(!m_ui->is_modal())
            m_settings.reset();
        return result;
    }

    Comet::Result<void> GameUi::apply_input(Comet::InputOverrides overrides) {
        if(!m_settings || !m_engine.get_scene_runtime().is_active())
            return Comet::Result<void>::failure(
                "Player input settings require an active Play session");
        auto resolved = overrides.resolve(m_project.input_actions());
        if(!resolved)
            return Comet::Result<void>::failure(resolved.error());
        if(auto saved = m_settings->save(std::move(overrides)); !saved)
            return saved;
        if(auto applied = m_engine.rebind_input_actions(std::move(resolved).value().actions);
            !applied)
            return Comet::Result<void>::failure(
                "Player settings saved but not applied: " + applied.error().message);
        return Comet::Result<void>::success();
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
