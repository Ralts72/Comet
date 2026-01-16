#include "runtime/entry.h"
#include "src/imgui_context.h"
#include "core/engine.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "core/window.h"
#include <imgui.h>
#include <memory>

class Editor final : public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("Editor initializing...");

        auto* renderer = get_engine()->get_renderer();
        auto* render_context = renderer->get_render_context();
        auto* scene_renderer = renderer->get_scene_renderer();

        m_imgui_layer = std::make_unique<CometEditor::ImGuiContext>(
            get_engine()->get_window(),
            render_context
        );

        setup_panels();

        // 注册 ImGui 渲染回调
        renderer->set_on_imgui_render([this](Comet::CommandBuffer& cmd) {
            m_imgui_layer->begin_frame();
            m_imgui_layer->end_frame();
            m_imgui_layer->render(cmd);
        });

        // 注册 Swapchain 重建回调
        scene_renderer->set_swapchain_recreate_callback([this]() {
            m_imgui_layer->recreate_swapchain();
        });

        LOG_INFO("Editor initialized");
    }

    void on_update(Comet::UpdateContext context) override {

    }

    void on_shutdown() override {
        LOG_INFO("Editor shutting down...");
        m_imgui_layer.reset();
    }

private:
    void setup_panels() {
        m_imgui_layer->set_ui_callback([this]() {
            // 使用透明背景的 DockSpace，让场景可见
            ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_PassthruCentralNode;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            render_menu_bar();
            render_hierarchy_panel();
            render_scene_panel();
            render_inspector_panel();
        });
    }

    void render_menu_bar() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {}
                if (ImGui::MenuItem("Open Scene", "Ctrl+O")) {}
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {}
                ImGui::Separator();
                if (ImGui::MenuItem("Exit", "Alt+F4")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z")) {}
                if (ImGui::MenuItem("Redo", "Ctrl+Y")) {}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                if (ImGui::MenuItem("Hierarchy")) {}
                if (ImGui::MenuItem("Inspector")) {}
                if (ImGui::MenuItem("Scene")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    void render_hierarchy_panel() {
        ImGui::Begin("Hierarchy");
        ImGui::Text("Scene Objects");
        ImGui::End();
    }

    void render_scene_panel() {
        ImGui::Begin("Scene");
        ImGui::Text("Scene Viewport");
        ImGui::End();
    }

    void render_inspector_panel() {
        ImGui::Begin("Inspector");
        ImGui::Text("Properties");
        ImGui::End();
    }

    std::unique_ptr<CometEditor::ImGuiContext> m_imgui_layer;
};

RUN_APP(Editor)
