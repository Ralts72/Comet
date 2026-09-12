#ifdef COMET_TEST_EDITOR_UI
#include "viewport/viewport.h"
#include "config/config.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "core/window.h"
#include "ui/imgui_context.h"
#include "ui/shortcuts.h"
#include "scene/selection.h"
#include "core/engine.h"
#include "render/scene/scene_extractor.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>

namespace CometEditor::Tests {
    TEST(ViewportTest, RendersAcrossSceneChangesAndIgnoresPlayPicking) {
        Comet::Config config;
        config.window.width = 640;
        config.window.height = 480;
        config.vulkan.enable_validation = true;
        Comet::Engine engine(config);
        auto& renderer = engine.get_renderer();
        renderer.enable_offscreen_rendering({320, 240});
        Comet::Tests::TemporaryDirectory directory;
        ImGuiContext ui(engine.get_window(), renderer.get_render_context(),
            directory.path() / "imgui.ini");
        Comet::Scene first_scene;
        Comet::Scene second_scene;
        auto* active_scene = &first_scene;
        SelectionService selection(first_scene);
        CommandHistory history;
        history.bind_scene(active_scene);
        auto components = Comet::create_scene_component_registry();
        PropertyEditTransaction edit(history, components);
        EditorState state;
        EditorShortcuts shortcuts;
        Viewport viewport(state, selection, history, components, edit, shortcuts,
            renderer, engine.get_asset_registry(), ui);
        ui.set_ui_callback([&] {
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 400});
            viewport.panel().render();
            viewport.update(active_scene);
        });
        renderer.set_overlay_callbacks(
            [&] {
                viewport.update_texture();
                ui.update_frame();
                viewport.submit_feedback(active_scene);
            },
            [&](Comet::CommandBuffer& command_buffer) { ui.render(command_buffer); });
        const auto draw_frame = [&] {
            engine.get_window().poll_events();
            if(!renderer.prepare_frame())
                return false;
            renderer.render_frame(Comet::SceneExtractor::extract(*active_scene));
            return true;
        };
        EXPECT_TRUE(draw_frame());
        EXPECT_TRUE(draw_frame());
        EXPECT_TRUE(viewport.panel().is_visible());

        active_scene = &second_scene;
        selection.set_scene(second_scene);
        history.bind_scene(active_scene);
        const auto selected = second_scene.create_entity();
        viewport.apply_pick(
            Comet::ScenePickHit{.entity_id = selected.get_id()}, active_scene);
        EXPECT_EQ(selection.get_selected_entity(), selected);
        EXPECT_TRUE(draw_frame());

        state.mode = EditorMode::Play;
        viewport.apply_pick(std::nullopt, active_scene);
        EXPECT_EQ(selection.get_selected_entity(), selected);
        EXPECT_TRUE(draw_frame());
        state.mode = EditorMode::Edit;
        viewport.apply_pick(std::nullopt, active_scene);
        EXPECT_FALSE(selection.get_selected_entity());
        viewport.panel().set_visible(false);
        EXPECT_TRUE(draw_frame());
        EXPECT_FALSE(viewport.panel().is_visible());

        renderer.set_overlay_callbacks({}, {});
        ui.set_ui_callback({});
        viewport.panel().cancel_interaction();
        renderer.get_render_context().wait_idle();
    }
}
#endif
