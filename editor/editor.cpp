#include "runtime/entry.h"
#include "asset/registry.h"
#include "common/geometry_utils.h"
#include "src/imgui_context.h"
#include "core/engine.h"
#include "render/material.h"
#include "render/renderer.h"
#include "render/scene_renderer.h"
#include "core/window.h"
#include "common/logger.h"
#include "menu_bar.h"
#include "src/panels/console.h"
#include "src/panels/inspector.h"
#include "src/panels/project.h"
#include "src/panels/view.h"
#include "src/panels/hierarchy.h"
#include "src/selection.h"
#include "scene/scene.h"

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <spdlog/sinks/callback_sink.h>

namespace {
    constexpr Comet::AssetHandle EDITOR_CUBE_MESH_HANDLE(1);
    constexpr Comet::AssetHandle EDITOR_CUBE_MATERIAL_HANDLE(2);

    void register_editor_render_assets(Comet::Engine& engine) {
        auto& resource_manager = engine.get_resource_manager();
        auto& asset_registry = engine.get_asset_registry();

        auto [cube_vertices, cube_indices] = Comet::GeometryUtils::create_cube(
            -0.5f, 0.5f, -0.5f, 0.5f, -0.5f, 0.5f);
        auto cube_mesh = resource_manager.create_mesh(
            "editor_demo_cube", cube_vertices, cube_indices);

        const std::string texture_path =
            std::string(PROJECT_ROOT_DIR) + "/engine/assets/textures/";
        const auto texture0 = resource_manager.load_texture(
            texture_path + "awesomeface.png");
        const auto texture1 = resource_manager.load_texture(
            texture_path + "R-C.jpeg");

        const Comet::MaterialConfig material_config;
        auto material = resource_manager.get_material_manager().create_material(
            "editor_demo_material", material_config);
        material->set_property_texture("u_Texture0", texture0);
        material->set_property_texture("u_Texture1", texture1);

        const bool mesh_registered = asset_registry.register_asset(
            EDITOR_CUBE_MESH_HANDLE, std::move(cube_mesh));
        const bool material_registered = asset_registry.register_asset(
            EDITOR_CUBE_MATERIAL_HANDLE, std::move(material));
        if(!mesh_registered || !material_registered) {
            LOG_FATAL("Failed to register editor demo render assets");
        }
    }

    struct EditorSceneSetup {
        std::unique_ptr<Comet::Scene> scene;
        Comet::EntityId cube_entity_id = Comet::INVALID_ENTITY_ID;
    };

    EditorSceneSetup create_editor_scene() {
        auto scene = std::make_unique<Comet::Scene>();
        Comet::Entity main_camera = scene->create_entity("Main Camera");
        main_camera.get_component<Comet::TransformComponent>().translation.z = 3.0f;
        main_camera.add_component<Comet::CameraComponent>().primary = true;

        Comet::Entity cube = scene->create_entity("Editor Cube");
        auto& transform = cube.get_component<Comet::TransformComponent>();
        transform.rotation = Comet::Math::Vec3(-20.0f, 30.0f, 0.0f);
        cube.add_component<Comet::MeshRendererComponent>(
            EDITOR_CUBE_MESH_HANDLE, EDITOR_CUBE_MATERIAL_HANDLE);
        const Comet::EntityId cube_entity_id = cube.get_id();

        return {
            .scene = std::move(scene),
            .cube_entity_id = cube_entity_id
        };
    }

    class Editor final: public Comet::Application {
    public:
        void on_init() override {
            LOG_INFO("Editor initializing...");

            auto& engine = get_engine();
            auto& renderer = engine.get_renderer();
            auto& render_context = renderer.get_render_context();
            auto& scene_renderer = renderer.get_scene_renderer();

            const auto* swapchain = render_context.get_swapchain();
            renderer.enable_viewport_rendering(
                Comet::Math::Vec2u(swapchain->get_width(), swapchain->get_height()));

            m_imgui_context = std::make_unique<CometEditor::ImGuiContext>(
                &engine.get_window(),
                &render_context,
                engine.get_config().vulkan
            );

            // 设置日志重定向
            setup_log_redirect();

            register_editor_render_assets(engine);
            EditorSceneSetup scene_setup = create_editor_scene();
            m_cube_entity_id = scene_setup.cube_entity_id;
            engine.set_scene(std::move(scene_setup.scene));
            auto& scene = *engine.get_scene();
            m_selection.emplace(scene);
            setup_panels(scene);

            m_imgui_context->set_viewport_images(
                scene_renderer.get_viewport_color_views(),
                renderer.get_resource_manager().get_sampler_manager().get_nearest_clamp());

            // 注册 ImGui 渲染回调
            renderer.set_on_imgui_render([this](Comet::CommandBuffer& cmd) {
                update_viewport_texture(
                    get_engine().get_renderer().get_scene_renderer());
                m_imgui_context->update_frame();
                m_imgui_context->render(cmd);
            });

            // 注册 Swapchain 重建回调
            scene_renderer.set_swapchain_recreate_callback([this]() {
                m_imgui_context->recreate_swapchain();
            });

            LOG_INFO("Editor initialized");
        }

        void on_update(Comet::UpdateContext context) override {
            // 更新 FPS 显示
            m_menu_bar->set_fps(context.fps);

            update_demo_cube(context.totalTime);
            update_viewport_state();
        }

        void update_viewport_state() {
            auto& renderer = get_engine().get_renderer();
            auto& scene_renderer = renderer.get_scene_renderer();
            const bool scene_visible = m_scene_view_panel->is_visible();
            const bool game_visible = m_game_view_panel->is_visible();

            if(scene_visible) {
                scene_renderer.set_render_mode(Comet::SceneRenderer::RenderMode::SceneView);
                renderer.request_viewport_resize(m_scene_view_panel->get_viewport_size());
            } else if(game_visible) {
                scene_renderer.set_render_mode(Comet::SceneRenderer::RenderMode::GameView);
                renderer.request_viewport_resize(m_game_view_panel->get_viewport_size());
            }
        }

        void update_viewport_texture(Comet::SceneRenderer& scene_renderer) {
            auto& renderer = get_engine().get_renderer();
            m_imgui_context->set_viewport_images(
                scene_renderer.get_viewport_color_views(),
                renderer.get_resource_manager().get_sampler_manager().get_nearest_clamp());

            const uint32_t frame_slot =
                scene_renderer.get_frame_manager().get_current_frame_slot_index();
            const ImTextureID texture_id =
                m_imgui_context->get_viewport_texture_id(frame_slot);
            const Comet::Math::Vec2u size = scene_renderer.get_render_target().get_size();
            m_scene_view_panel->set_texture_id(texture_id, size.x, size.y);
            m_game_view_panel->set_texture_id(texture_id, size.x, size.y);
        }

        void on_shutdown() override {
            LOG_INFO("Editor shutting down...");
            m_imgui_context.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_selection.reset();
        }

    private:
        void update_demo_cube(const float total_time) {
            Comet::Scene* scene = get_engine().get_scene();
            if(!scene) {
                return;
            }

            if(Comet::Entity cube = scene->find_entity(m_cube_entity_id)) {
                cube.get_component<Comet::TransformComponent>().rotation.y =
                    30.0f + total_time * 45.0f;
            }
        }

        void setup_log_redirect() const {
            // 移除控制台输出，只保留文件输出和 GUI 输出
            Comet::Logger::remove_console_sinks();

            // 创建 callback sink，将日志发送到 ConsolePanel
            const auto gui_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [this](const spdlog::details::log_msg& msg) {
                    const Comet::LogLevel level = Comet::log_level_from_spdlog(msg.level);
                    const std::string message(msg.payload.data(), msg.payload.size());
                    if(m_console_panel) {
                        m_console_panel->add_log(level, message);
                    }
                }
            );

            // 将 GUI sink 添加到 logger
            Comet::Logger::add_custom_sink(gui_sink);
        }

        void setup_panels(Comet::Scene& scene) {
            // 创建菜单栏
            m_menu_bar = std::make_unique<CometEditor::MenuBar>();

            // 创建面板
            m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>(scene, *m_selection);
            m_scene_view_panel = std::make_unique<CometEditor::ViewPanel>(CometEditor::ViewType::SceneView);
            m_game_view_panel = std::make_unique<CometEditor::ViewPanel>(CometEditor::ViewType::GameView);
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(*m_selection);
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>();
            m_console_panel = std::make_unique<CometEditor::ConsolePanel>();

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
                const ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
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
        std::optional<CometEditor::SelectionService> m_selection;
        Comet::EntityId m_cube_entity_id = Comet::INVALID_ENTITY_ID;

        std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
        std::unique_ptr<CometEditor::ViewPanel> m_scene_view_panel;
        std::unique_ptr<CometEditor::ViewPanel> m_game_view_panel;
        std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
        std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
        std::unique_ptr<CometEditor::ConsolePanel> m_console_panel;
    };
}

RUN_APP(Editor)
