#ifdef COMET_TEST_EDITOR_UI
#include "project/game_ui.h"
#include "ui/imgui_context.h"
#include "support/render_gpu_test.h"
#include "support/temporary_directory.h"
#include "scene/scene.h"
#include "core/window.h"
#include "graphics/resource/sampler.h"
#include "input/player_input_settings.h"

#include <RmlUi/Core.h>
#include <imgui.h>
#include <fstream>

namespace CometEditor::Tests {
    class EditorGameUiGpuTest: public Comet::Tests::RenderGpuTest {
    protected:
        std::optional<Comet::Project> project;
        std::optional<Comet::PlayerInputSettings> player_input;
        std::unique_ptr<GameUi> game;
        std::unique_ptr<Ui::ImGuiContext> editor;
        std::shared_ptr<Comet::Sampler> sampler;
        bool visible = true;
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
            game = std::make_unique<GameUi>(*engine, *project, input_services());
            ASSERT_NE(Rml::GetContext(0), nullptr);
            renderer.set_overlay(
                {.render =
                        [this](Comet::OverlayRecordContext& overlay) {
                            auto result = game->render(overlay);
                            if(!result)
                                return result;
                            editor->render(overlay.command_buffer());
                            return Comet::Result<void, Comet::GraphicsError>::success();
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
            editor.reset();
            sampler.reset();
            RenderGpuTest::TearDown();
        }

        void frame() {
            auto& renderer = engine->get_renderer();
            auto& window = engine->get_window();
            for(unsigned attempt = 0; attempt < 12; ++attempt) {
                window.poll_events();
                auto prepared = renderer.prepare_frame();
                ASSERT_TRUE(prepared) << prepared.error();
                if(prepared.value() != Comet::Renderer::FramePreparation::Ready)
                    continue;
                const auto output = renderer.get_offscreen_frame();
                ASSERT_TRUE(editor->begin_frame());
                editor->set_viewport_image(output.slot, output.color_view, sampler);
                ImGui::Begin("Game preview");
                const auto origin = ImGui::GetCursorScreenPos();
                ImGui::Image(editor->get_viewport_texture_id(output.slot), {120, 90});
                ImGui::End();
                std::optional<Comet::Ui::View> view;
                if(visible)
                    view = Comet::Ui::View{.origin = {origin.x, origin.y},
                        .size = {120, 90},
                        .pixel_size = output.size,
                        .density = 1};
                const auto ui = game->frame(window.publish_input_frame(),
                    {.fps = 60,
                        .game_available = engine->get_scene_runtime().is_active(),
                        .view = view});
                ASSERT_TRUE(ui) << ui.error().message;
                editor->end_frame();
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

    TEST_F(EditorGameUiGpuTest, InvalidInitialControllerKeepsEditorUsableAndReloadRetries) {
        engine->get_renderer().set_overlay({});
        engine->get_renderer().wait_idle();
        game.reset();
        const auto file = documents.path() / "assets/ui/runtime.ui.lua";
        {
            std::ofstream script(file, std::ios::trunc);
            script << "return {on_mount=42}";
        }
        game = std::make_unique<GameUi>(*engine, *project, input_services());
        EXPECT_FALSE(game->is_modal());
        engine->get_renderer().set_overlay({.render = [this](Comet::OverlayRecordContext& overlay) {
            auto result = game->render(overlay);
            if(!result)
                return result;
            editor->render(overlay.command_buffer());
            return Comet::Result<void, Comet::GraphicsError>::success();
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
