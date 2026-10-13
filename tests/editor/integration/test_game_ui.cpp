#ifdef COMET_TEST_EDITOR_UI
#include "project/game_ui.h"
#include "ui/imgui_context.h"
#include "support/render_gpu_test.h"
#include "support/temporary_directory.h"
#include "scene/scene.h"
#include "scene/selection.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "scene/component_registry.h"
#include "core/window.h"
#include "common/scope_exit.h"
#include "graphics/resource/sampler.h"
#include "input/player_input_settings.h"
#include "viewport/viewport_panel.h"
#include "viewport/transform_gizmo.h"
#include "ui/shortcuts.h"

#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>
#include <imgui.h>
#include <fstream>
#include <sstream>
#include <string_view>

namespace CometEditor::Tests {
    class EditorGameUiGpuTest: public Comet::Tests::RenderGpuTest {
    protected:
        std::optional<Comet::Project> project;
        std::optional<Comet::PlayerInputSettings> player_input;
        std::unique_ptr<GameUi> game;
        std::unique_ptr<Ui::ImGuiContext> editor;
        std::unique_ptr<Ui::ImGuiContext::Frame> editor_frame;
        std::shared_ptr<Comet::Sampler> sampler;
        bool visible = true;
        bool docked = false;
        std::optional<Comet::Math::Vec2> pointer;
        std::optional<Comet::Ui::View> ui_view;
        bool ui_blocked = false;
        Comet::Tests::TemporaryDirectory documents;

        void SetUp() override {
            RenderGpuTest::SetUp();
            ASSERT_TRUE(engine);
            const auto demo = std::filesystem::path(PROJECT_ROOT_DIR) / "demo";
            std::filesystem::create_directories(documents.path() / "assets/ui");
            std::filesystem::copy_file(demo / "project.json", documents.path() / "project.json");
            for(const auto* file : {"runtime.rml", "runtime.rcss", "runtime.ui.lua"})
                std::filesystem::copy_file(
                    demo / "assets/ui" / file, documents.path() / "assets/ui" / file);
            auto loaded = Comet::Project::load(documents.path());
            ASSERT_TRUE(loaded) << loaded.error();
            project = std::move(loaded).value();
            auto& renderer = engine->get_renderer();
            ASSERT_TRUE(renderer.enable_offscreen_rendering({960, 720}));
            auto created =
                Ui::ImGuiContext::create(engine->get_window(), renderer.get_render_context(),
                    {.composition = Ui::ImGuiContext::Composition::Clear});
            ASSERT_TRUE(created) << created.error();
            editor = std::move(created).value();
            auto sampling = Comet::Sampler::create(renderer.get_render_context().get_device());
            ASSERT_TRUE(sampling) << sampling.error();
            sampler = std::move(sampling).value();
            auto settings = Comet::PlayerInputSettings::load(
                project->id(), documents.path() / "player/input.json");
            ASSERT_TRUE(settings) << settings.error();
            player_input = std::move(settings).value();
            game = std::make_unique<GameUi>(
                engine->get_window(), renderer, *project, input_services());
            ASSERT_NE(Rml::GetContext(0), nullptr);
            renderer.set_overlay({.capture =
                                      [this] {
                                          Comet::OverlayDrawList draws;
                                          if(auto frame = game->take_draw_frame())
                                              draws.push_back(std::move(frame));
                                          if(editor_frame)
                                              draws.push_back(std::move(editor_frame));
                                          return draws;
                                      },
                .release =
                    [this] {
                        game->release();
                        editor->release_swapchain_resources();
                    },
                .rebuild =
                    [this](const Comet::SwapchainCompatibility& compatibility) {
                        auto result = game->rebuild(compatibility);
                        if(!result)
                            return result;
                        return editor->rebuild_swapchain_resources(compatibility);
                    }});
            engine->set_scene(std::make_unique<Comet::Scene>());
            ASSERT_TRUE(engine->set_input_actions(project->input_actions()));
        }

        void TearDown() override {
            if(engine) {
                engine->get_renderer().set_overlay({});
                engine->get_renderer().wait_idle();
            }
            if(game)
                game->deactivate();
            game.reset();
            editor_frame.reset();
            editor.reset();
            sampler.reset();
            RenderGpuTest::TearDown();
        }

        void frame(ViewportPanel* viewport = nullptr, std::optional<bool> mouse_down = {}) {
            auto& renderer = engine->get_renderer();
            auto& window = engine->get_window();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                window.poll_events();
                auto prepared = renderer.prepare_frame();
                ASSERT_TRUE(prepared) << prepared.error();
                if(prepared.value() != Comet::Renderer::FramePreparation::Ready)
                    continue;
                if(viewport) {
                    // 脚本化输入在 poll 后交付，避免系统窗口焦点变化污染测试事件。
                    const auto focus = glfwSetWindowFocusCallback(window.get(), nullptr);
                    glfwSetWindowFocusCallback(window.get(), focus);
                    ASSERT_NE(focus, nullptr);
                    focus(window.get(), GLFW_TRUE);
                    if(pointer) {
                        const auto enter = glfwSetCursorEnterCallback(window.get(), nullptr);
                        glfwSetCursorEnterCallback(window.get(), enter);
                        ASSERT_NE(enter, nullptr);
                        enter(window.get(), GLFW_TRUE);
                        const auto cursor = glfwSetCursorPosCallback(window.get(), nullptr);
                        glfwSetCursorPosCallback(window.get(), cursor);
                        ASSERT_NE(cursor, nullptr);
                        cursor(window.get(), pointer->x, pointer->y);
                    }
                    if(mouse_down) {
                        const auto mouse = glfwSetMouseButtonCallback(window.get(), nullptr);
                        glfwSetMouseButtonCallback(window.get(), mouse);
                        ASSERT_NE(mouse, nullptr);
                        mouse(window.get(), GLFW_MOUSE_BUTTON_LEFT,
                            *mouse_down ? GLFW_PRESS : GLFW_RELEASE, 0);
                    }
                }
                const auto output = renderer.get_offscreen_frame();
                ASSERT_TRUE(editor->begin_frame());
                editor->set_viewport_image(output.slot, output.color_view, sampler);
                auto input = window.publish_input_frame();
                std::optional<Comet::Ui::View> view;
                if(viewport) {
                    if(docked) {
                        const auto dockspace = ImGui::DockSpaceOverViewport();
                        ImGui::SetNextWindowDockID(dockspace, ImGuiCond_Always);
                    } else {
                        ImGui::SetNextWindowPos({20, 40});
                        ImGui::SetNextWindowSize({900, 650});
                    }
                    viewport->set_texture_id(
                        editor->get_viewport_texture_id(output.slot), output.size.x, output.size.y);
                    viewport->render();
                    view = viewport->game_ui_view(output.size);
                    input = viewport->route_game_ui_input(input, false);
                } else {
                    ImGui::Begin("Game preview");
                    const auto origin = ImGui::GetCursorScreenPos();
                    ImGui::Image(editor->get_viewport_texture_id(output.slot), {120, 90});
                    ImGui::End();
                    if(visible)
                        view = Comet::Ui::View{.origin = {origin.x, origin.y},
                            .size = {120, 90},
                            .pixel_size = output.size,
                            .density = 1};
                }
                const auto ui = game->frame(
                    input, {.fps = 60,
                               .game_available = engine->get_scene_runtime().is_active(),
                               .view = view});
                ui_view = view;
                ASSERT_TRUE(ui) << ui.error().message;
                ui_blocked = ui.value().blocked;
                editor_frame = editor->end_frame();
                ASSERT_TRUE(renderer.render_frame());
                return;
            }
            FAIL() << "Editor game preview remained deferred";
        }

        void click(const char* id) {
            auto* context = Rml::GetContext(0);
            ASSERT_NE(context, nullptr);
            auto* document = context->GetDocument(0);
            ASSERT_NE(document, nullptr);
            auto* button = document->GetElementById(id);
            ASSERT_NE(button, nullptr);
            button->Click();
            frame();
        }

        void settings() { click("settings"); }

        void override_controller_callback(std::string_view name, std::string_view body) {
            std::ifstream original(
                std::filesystem::path(PROJECT_ROOT_DIR) / "demo/assets/ui/runtime.ui.lua");
            ASSERT_TRUE(original);
            const std::string source{
                std::istreambuf_iterator<char>(original), std::istreambuf_iterator<char>()};
            std::ofstream script(documents.path() / "assets/ui/runtime.ui.lua", std::ios::trunc);
            ASSERT_TRUE(script);
            script << "local controller = (function()\n"
                   << source << "\nend)()\ncontroller." << name << " = function(self, ui) " << body
                   << " end\nreturn controller";
            script.flush();
            ASSERT_TRUE(script.good());
        }

        Comet::Ui::ProjectUi::Services input_services() {
            return {.load_input =
                        [this] {
                            return Comet::Result<Comet::InputOverrides>::success(
                                player_input->overrides());
                        },
                .apply_input =
                    [this](Comet::InputOverrides overrides) {
                        if(!engine->get_scene_runtime().is_active())
                            return Comet::Result<void>::failure("No active Play session");
                        return player_input->save_and_apply(project->input_actions(),
                            std::move(overrides), [this](Comet::InputActions actions) {
                                return engine->rebind_input_actions(std::move(actions));
                            });
                    }};
        }
    };

    TEST_F(EditorGameUiGpuTest, MouseClickThroughPlayViewportOpensSettingsWhileRunningAndPaused) {
        auto& window = engine->get_window();
        glfwSetWindowSize(window.get(), 960, 720);
        ASSERT_TRUE(engine->start_scene_runtime());
        EditorState state;
        state.mode = EditorMode::Play;
        auto& scene = *engine->get_scene();
        SelectionService selection(scene);
        CommandHistory history;
        history.bind_scene(&scene);
        auto components = Comet::create_scene_component_registry();
        PropertyEditTransaction edit(history, components);
        TransformGizmo gizmo(history, components);
        EditorShortcuts shortcuts;
        ViewportPanel viewport(
            state, engine->get_scene_runtime(), selection, gizmo, edit, 4096, shortcuts);
        viewport.set_game_ui_available(true);
        struct Scenario {
            bool docked;
            Comet::SceneRuntime::State state;
        };
        for(const auto scenario : {Scenario{false, Comet::SceneRuntime::State::Running},
                Scenario{false, Comet::SceneRuntime::State::Paused},
                Scenario{true, Comet::SceneRuntime::State::Running},
                Scenario{true, Comet::SceneRuntime::State::Paused}}) {
            SCOPED_TRACE(::testing::Message() << "docked=" << scenario.docked
                                              << " state=" << static_cast<int>(scenario.state));
            docked = scenario.docked;
            ASSERT_TRUE(engine->set_runtime_state(scenario.state));
            frame(&viewport);
            frame(&viewport);
            const auto view = ui_view;
            ASSERT_TRUE(view);
            auto* settings = Rml::GetContext(0)->GetDocument(0)->GetElementById("settings");
            ASSERT_NE(settings, nullptr);
            const auto offset = settings->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto size = settings->GetBox().GetSize(Rml::BoxArea::Border);
            pointer = Comet::Math::Vec2{
                view->origin.x + (offset.x + size.x / 2) * view->size.x / view->pixel_size.x,
                view->origin.y + (offset.y + size.y / 2) * view->size.y / view->pixel_size.y};
            frame(&viewport);
            frame(&viewport);
            frame(&viewport, true);
            ASSERT_TRUE(viewport.route_game_ui_input(window.get_input_frame(), false).focused);
            frame(&viewport);
            frame(&viewport, false);
            EXPECT_TRUE(game->is_modal());
            game->deactivate();
            frame(&viewport);
            EXPECT_FALSE(game->is_modal());
        }
    }

    TEST_F(
        EditorGameUiGpuTest, PreviewPlayPauseHideAndStopReuseProjectControllerAndImGuiComposition) {
        frame();
        settings();
        EXPECT_FALSE(game->is_modal());
        for(int session = 0; session < 2; ++session) {
            ASSERT_TRUE(engine->start_scene_runtime());
            frame();
            settings();
            EXPECT_TRUE(game->is_modal());
            ASSERT_TRUE(engine->set_runtime_state(Comet::SceneRuntime::State::Paused));
            frame();
            EXPECT_TRUE(game->is_modal());
            ASSERT_TRUE(engine->request_runtime_step());
            frame();
            EXPECT_TRUE(game->is_modal());
            visible = false;
            frame();
            EXPECT_FALSE(game->is_modal());
            visible = true;
            frame();
            settings();
            EXPECT_TRUE(game->is_modal());
            game->deactivate();
            ASSERT_TRUE(engine->stop_scene_runtime());
            frame();
            EXPECT_FALSE(game->is_modal());
            EXPECT_EQ(Rml::GetNumContexts(), 1);
        }
        const auto old = Rml::GetContext(0)->GetDocument(0);
        game->reload();
        frame();
        EXPECT_NE(Rml::GetContext(0)->GetDocument(0), old);
        EXPECT_EQ(Rml::GetNumContexts(), 1);
    }

    TEST_F(EditorGameUiGpuTest, ResetAndReloadKeepCompositionUsableDuringTheRenderLoop) {
        auto& renderer = engine->get_renderer();
        unsigned frames = 0;
        bool reset = false;
        const auto result = engine->run({
            .update =
                [&](const Comet::Engine::FrameContext&) {
                    if(frames == 1 && !reset) {
                        game->reset();
                        reset = true;
                        EXPECT_EQ(Rml::GetNumContexts(), 1);
                        EXPECT_FALSE(game->is_modal());
                    }
                    return Comet::Result<void, Comet::Error>::success();
                },
            .frame_ready =
                [&](const Comet::Engine::FrameContext& frame) {
                    if(frames == 1)
                        game->reload();
                    const auto output = renderer.get_offscreen_frame();
                    if(!editor->begin_frame())
                        return Comet::Result<void, Comet::Error>::failure(
                            {"Cannot begin editor UI frame"});
                    editor->set_viewport_image(output.slot, output.color_view, sampler);
                    ImGui::Begin("Game preview");
                    ImGui::Image(editor->get_viewport_texture_id(output.slot), {120, 90});
                    ImGui::End();
                    const auto prepared = game->frame(frame.physical_input,
                        {.view = Comet::Ui::View{.size = Comet::Math::Vec2(output.size),
                             .pixel_size = output.size,
                             .density = 1}});
                    if(!prepared)
                        return Comet::Result<void, Comet::Error>::failure(prepared.error());
                    editor_frame = editor->end_frame();
                    EXPECT_NE(Rml::GetContext(0)->GetDocument(0), nullptr);
                    if(++frames == 3)
                        engine->get_window().request_close();
                    return Comet::Result<void, Comet::Error>::success();
                },
        });
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_TRUE(reset);
        EXPECT_EQ(frames, 3u);
        EXPECT_EQ(Rml::GetNumContexts(), 1);
    }

    TEST_F(EditorGameUiGpuTest, InvalidInitialControllerKeepsEditorUsableAndReloadRetries) {
        engine->get_renderer().set_overlay({});
        engine->get_renderer().wait_idle();
        game.reset();
        const auto file = documents.path() / "assets/ui/runtime.ui.lua";
        {
            std::ofstream script(file, std::ios::trunc);
            script << "return {on_mount=42}";
        }
        game = std::make_unique<GameUi>(
            engine->get_window(), engine->get_renderer(), *project, input_services());
        EXPECT_FALSE(game->is_modal());
        engine->get_renderer().set_overlay({.capture = [this] {
            Comet::OverlayDrawList draws;
            if(auto frame = game->take_draw_frame())
                draws.push_back(std::move(frame));
            if(editor_frame)
                draws.push_back(std::move(editor_frame));
            return draws;
        }});
        frame();
        std::filesystem::copy_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "demo/assets/ui/runtime.ui.lua", file,
            std::filesystem::copy_options::overwrite_existing);
        game->reload();
        frame();
        ASSERT_EQ(Rml::GetNumContexts(), 1);
        ASSERT_TRUE(engine->start_scene_runtime());
        frame();
        settings();
        EXPECT_TRUE(game->is_modal());
    }

    TEST_F(EditorGameUiGpuTest, ControllerFailuresReleaseInputAndReloadWithoutChangingTheScene) {
        auto* scene = engine->get_scene();
        const auto components = Comet::create_scene_component_registry();
        CommandHistory history;
        history.bind_scene(scene);
        ASSERT_TRUE(SceneCommands::create_entity(history, components, "Unsaved edit"));
        const auto dirty_state = history.state_id();
        ASSERT_TRUE(engine->start_scene_runtime());
        frame();
        for(const auto* method : {"on_frame", "on_event"}) {
            SCOPED_TRACE(method);
            const auto file = documents.path() / "assets/ui/runtime.ui.lua";
            ASSERT_NO_FATAL_FAILURE(
                override_controller_callback(method, "error('UI runtime failure')"));
            game->reload();
            ASSERT_TRUE(game->error().empty());
            if(std::string_view(method) == "on_event") {
                frame();
                settings();
            } else {
                frame();
            }
            EXPECT_NE(game->error().find("UI runtime failure"), std::string::npos);
            EXPECT_FALSE(game->is_modal());
            EXPECT_TRUE(ui_blocked);
            const auto expected = "Project UI stopped: " + game->error()
                                  + "; use Reload UI after fixing the controller";
            std::istringstream diagnostics(messages.str());
            std::string line, remaining;
            unsigned expected_count = 0;
            while(std::getline(diagnostics, line)) {
                if(line.ends_with(" [error] " + expected) && line.find("VUID-") == std::string::npos
                    && line.find("Validation Error") == std::string::npos)
                    ++expected_count;
                else
                    remaining += line + '\n';
            }
            EXPECT_EQ(expected_count, 1u) << messages.str();
            messages.str(remaining);
            messages.clear();
            EXPECT_EQ(engine->get_scene(), scene);
            EXPECT_EQ(history.state_id(), dirty_state);
            EXPECT_TRUE(history.can_undo());
            EXPECT_TRUE(engine->get_scene_runtime().is_active());
            EXPECT_FALSE(engine->get_window().should_close());
            frame();
            EXPECT_FALSE(ui_blocked);
            std::filesystem::copy_file(
                std::filesystem::path(PROJECT_ROOT_DIR) / "demo/assets/ui/runtime.ui.lua", file,
                std::filesystem::copy_options::overwrite_existing);
            game->reload();
            EXPECT_TRUE(game->error().empty());
            frame();
            settings();
            EXPECT_TRUE(game->is_modal());
            game->deactivate();
            frame();
        }
    }

    TEST_F(EditorGameUiGpuTest, BackendFailuresRemainFatalToTheHost) {
        ASSERT_NO_FATAL_FAILURE(
            override_controller_callback("on_frame", "ui.property('hud','filter','blur(1px)')"));
        game->reload();
        ASSERT_TRUE(game->error().empty());
        auto& renderer = engine->get_renderer();
        bool ready = false;
        for(unsigned attempt = 0; attempt < 12 && !ready; ++attempt) {
            engine->get_window().poll_events();
            const auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error().message;
            ready = prepared.value() == Comet::Renderer::FramePreparation::Ready;
        }
        ASSERT_TRUE(ready);
        const Comet::ScopeExit finish_frame([&] {
            const auto finished = renderer.render_frame();
            EXPECT_TRUE(finished) << finished.error().message;
        });
        const auto output = renderer.get_offscreen_frame();
        ASSERT_TRUE(editor->begin_frame());
        const Comet::ScopeExit finish_editor([&] { editor_frame = editor->end_frame(); });
        editor->set_viewport_image(output.slot, output.color_view, sampler);
        ImGui::Begin("Game preview");
        ImGui::Image(editor->get_viewport_texture_id(output.slot), {120, 90});
        ImGui::End();
        const auto result = game->frame(engine->get_window().publish_input_frame(),
            {.view = Comet::Ui::View{
                 .size = Comet::Math::Vec2(output.size), .pixel_size = output.size, .density = 1}});
        ASSERT_FALSE(result);
        EXPECT_FALSE(Comet::Ui::is_controller_error(result.error()));
        EXPECT_NE(result.error().message.find("not supported"), std::string::npos);
        EXPECT_TRUE(game->error().empty());
    }

    TEST_F(EditorGameUiGpuTest, ReopenedMenuUsesSharedHostSettingsAndApplyUpdatesThatSameOwner) {
        ASSERT_TRUE(engine->start_scene_runtime());
        frame();
        settings();
        ASSERT_TRUE(game->is_modal());
        game->deactivate();
        frame();

        const auto& actions = project->input_actions().actions();
        ASSERT_FALSE(actions.empty());
        ASSERT_FALSE(actions.front().bindings.empty());
        const auto disabled =
            Comet::InputOverrides::create({{actions.front().id, actions.front().type, false,
                {{.id = actions.front().bindings.front().id, .disabled = true}}}});
        ASSERT_TRUE(disabled) << disabled.error();
        // 模拟另一种呈现层通过宿主提交；项目 UI 不另读一份设置快照。
        ASSERT_TRUE(input_services().apply_input(disabled.value()));
        settings();
        auto* bindings = Rml::GetContext(0)->GetDocument(0)->GetElementById("bindings");
        ASSERT_NE(bindings, nullptr);
        EXPECT_NE(bindings->GetInnerRML().find("已禁用"), std::string::npos);

        click("restore");
        click("apply");
        EXPECT_FALSE(game->is_modal());
        EXPECT_TRUE(player_input->overrides().actions().empty());
        const auto saved = Comet::PlayerInputSettings::load(project->id(), player_input->path());
        ASSERT_TRUE(saved) << saved.error();
        EXPECT_EQ(saved.value().overrides(), player_input->overrides());
        game->reload();
        frame();
        settings();
        bindings = Rml::GetContext(0)->GetDocument(0)->GetElementById("bindings");
        ASSERT_NE(bindings, nullptr);
        EXPECT_EQ(bindings->GetInnerRML().find("已禁用"), std::string::npos);
    }
}
#endif
