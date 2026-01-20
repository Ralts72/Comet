#include "runtime/entry.h"
#include "src/imgui_context.h"
#include "core/engine.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "core/window.h"
#include "common/logger.h"
#include "menu_bar.h"
#include "src/panels/console_panel.h"
#include "src/panels/inspector_panel.h"
#include "src/panels/project_panel.h"
#include "src/panels/view_panel.h"
#include "src/panels/hierarchy_panel.h"
#include <spdlog/sinks/callback_sink.h>

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

        // 设置日志重定向
        setup_log_redirect();

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
        // 更新 FPS 显示
        m_menu_bar->set_fps(context.fps);

        update_viewport_textures();
    }

    void update_viewport_textures() {
        bool scene_visible = m_scene_view_panel->is_visible();
        bool game_visible = m_game_view_panel->is_visible();

        if (scene_visible) {
            render_scene_view();
        } else if (game_visible) {
            render_game_view();
        }
    }

    void render_scene_view() {
        auto* renderer = get_engine()->get_renderer();
        auto* scene_renderer = renderer->get_scene_renderer();

        // 设置渲染模式为 Scene View（set_render_mode 内部会检查模式是否改变）
        scene_renderer->set_render_mode(Comet::SceneRenderer::RenderMode::SceneView);

        // TODO: 实现 Scene View 的渲染逻辑
        // 1. 使用 Editor Camera 的 view/projection 矩阵
        // 2. 渲染场景到离屏渲染目标
        // 3. 获取渲染目标的纹理 ID
        // 4. 更新 ViewPanel 的 Scene View 纹理

        // 临时实现：使用 swapchain 的当前图像（需要后续改为离屏渲染目标）
        // 注意：这只是占位实现，实际应该使用独立的离屏渲染目标
        auto* swapchain = renderer->get_render_context()->get_swapchain();
        uint32_t width = swapchain->get_width();
        uint32_t height = swapchain->get_height();

        // TODO: 获取离屏渲染目标的纹理 ID
        // 目前暂时不更新，等待离屏渲染目标实现
        // m_scene_view_panel->set_texture_id(texture_id, width, height);
    }

    void render_game_view() {
        auto* renderer = get_engine()->get_renderer();
        auto* scene_renderer = renderer->get_scene_renderer();

        // 设置渲染模式为 Game View（set_render_mode 内部会检查模式是否改变）
        scene_renderer->set_render_mode(Comet::SceneRenderer::RenderMode::GameView);

        // TODO: 实现 Game View 的渲染逻辑
        // 1. 使用 Game Camera 的 view/projection 矩阵
        // 2. 渲染场景到离屏渲染目标（或使用 swapchain）
        // 3. 获取渲染目标的纹理 ID
        // 4. 更新 ViewPanel 的 Game View 纹理

        // 临时实现：使用 swapchain 的当前图像（需要后续改为离屏渲染目标）
        auto* swapchain = renderer->get_render_context()->get_swapchain();
        uint32_t width = swapchain->get_width();
        uint32_t height = swapchain->get_height();

        // TODO: 获取离屏渲染目标的纹理 ID
        // 目前暂时不更新，等待离屏渲染目标实现
        // m_scene_view_panel->set_texture_id(texture_id, width, height);
    }

    void on_shutdown() override {
        LOG_INFO("Editor shutting down...");
        m_imgui_context.reset();
    }

private:
    void setup_log_redirect() {
        // 移除控制台输出，只保留文件输出和 GUI 输出
        Comet::Logger::remove_console_sinks();

        // 创建 callback sink，将日志发送到 ConsolePanel
        auto gui_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
            [this](const spdlog::details::log_msg& msg) {
                Comet::LogLevel level = Comet::log_level_from_spdlog(msg.level);
                std::string message(msg.payload.data(), msg.payload.size());
                if (m_console_panel) {
                    m_console_panel->add_log(level, message);
                }
            }
        );

        // 将 GUI sink 添加到 logger
        Comet::Logger::add_custom_sink(gui_sink);
    }

    void setup_panels() {
        // 创建菜单栏
        m_menu_bar = std::make_unique<CometEditor::MenuBar>();

        // 创建面板
        m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>();
        m_scene_view_panel = std::make_unique<CometEditor::ViewPanel>(CometEditor::ViewType::SceneView);
        m_game_view_panel = std::make_unique<CometEditor::ViewPanel>(CometEditor::ViewType::GameView);
        m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>();
        m_project_panel = std::make_unique<CometEditor::ProjectPanel>();
        m_console_panel = std::make_unique<CometEditor::ConsolePanel>();

        // 设置 Hierarchy 选择回调
        m_hierarchy_panel->set_selection_callback([this](const std::string& object_name) {
            m_inspector_panel->set_selected_object(object_name);
        });

        // 设置菜单栏面板可见性回调
        m_menu_bar->set_panel_visibility_callback("Hierarchy", [this](const bool visible) {
            m_hierarchy_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("SceneView", [this](const bool visible) {
            m_scene_view_panel->set_visible(visible);
        });
        m_menu_bar->set_panel_visibility_callback("GameView", [this](const bool visible) {
            m_game_view_panel->set_visible(visible);
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
            m_hierarchy_panel->render();
            m_scene_view_panel->render();
            m_game_view_panel->render();
            m_inspector_panel->render();
            m_project_panel->render();
            m_console_panel->render();
        });
    }
    std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
    std::unique_ptr<CometEditor::MenuBar> m_menu_bar;

    std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
    std::unique_ptr<CometEditor::ViewPanel> m_scene_view_panel;
    std::unique_ptr<CometEditor::ViewPanel> m_game_view_panel;
    std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
    std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
    std::unique_ptr<CometEditor::ConsolePanel> m_console_panel;
};

RUN_APP(Editor)
