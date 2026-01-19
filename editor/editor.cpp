#include "runtime/entry.h"
#include "src/imgui_context.h"
#include "core/engine.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "core/window.h"
#include "menu_bar.h"
#include "src/panels/console_panel.h"
#include "src/panels/inspector_panel.h"
#include "src/panels/project_panel.h"
#include "src/panels/viewport_panel.h"
#include "src/panels/world_panel.h"

class Editor final : public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("Editor initializing...");

        auto* renderer = get_engine()->get_renderer();
        auto* render_context = renderer->get_render_context();
        auto* scene_renderer = renderer->get_scene_renderer();

        m_imgui_context = std::make_unique<CometEditor::ImGuiContext>(
            get_engine()->get_window(),
            render_context
        );

        setup_panels();

        // 注册 ImGui 渲染回调
        renderer->set_on_imgui_render([this](Comet::CommandBuffer& cmd) {
            m_imgui_context->update_frame();
            m_imgui_context->render(cmd);
        });

        // 注册 Swapchain 重建回调
        scene_renderer->set_swapchain_recreate_callback([this]() {
            m_imgui_context->recreate_swapchain();
        });

        LOG_INFO("Editor initialized");
    }

    void on_update(Comet::UpdateContext context) override {

    }

    void on_shutdown() override {
        LOG_INFO("Editor shutting down...");
        m_imgui_context.reset();
    }

private:
    void setup_panels() {
        // 创建菜单栏
        m_menu_bar = std::make_unique<CometEditor::MenuBar>();

        // 创建面板
        m_world_panel = std::make_unique<CometEditor::WorldPanel>();
        m_viewport_panel = std::make_unique<CometEditor::ViewportPanel>();
        m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>();
        m_project_panel = std::make_unique<CometEditor::ProjectPanel>();
        m_console_panel = std::make_unique<CometEditor::ConsolePanel>();

        // 设置 World 选择回调
        m_world_panel->set_selection_callback([this](const std::string& object_name) {
            m_inspector_panel->set_selected_object(object_name);
        });

        // 设置菜单栏面板可见性回调
        m_menu_bar->set_panel_visibility_callback("World", [this](const bool visible) {
            m_world_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("Viewport", [this](const bool visible) {
            m_viewport_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("Inspector", [this](const bool visible) {
            m_inspector_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("Project", [this](const bool visible) {
            m_project_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("Log", [this](const bool visible) {
            m_console_panel->set_visible(visible);
        });

        // 设置 UI 回调
        m_imgui_context->set_ui_callback([this]() {
            // 使用透明背景的 DockSpace，让场景可见
            ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            m_menu_bar->render();
            m_world_panel->render();
            m_viewport_panel->render();
            m_inspector_panel->render();
            m_project_panel->render();
            m_console_panel->render();
        });
    }
    std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
    std::unique_ptr<CometEditor::MenuBar> m_menu_bar;

    std::unique_ptr<CometEditor::WorldPanel> m_world_panel;
    std::unique_ptr<CometEditor::ViewportPanel> m_viewport_panel;
    std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
    std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
    std::unique_ptr<CometEditor::ConsolePanel> m_console_panel;
};

RUN_APP(Editor)
