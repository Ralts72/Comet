#ifdef COMET_TEST_EDITOR_UI
#include "viewport/viewport.h"
#include "config/config.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/resource/render_resources.h"
#include "core/window.h"
#include "graphics/swapchain.h"
#include "graphics/resource/sampler.h"
#include "ui/imgui_context.h"
#include "ui/shortcuts.h"
#include "scene/selection.h"
#include "core/engine.h"
#include "render/scene/scene_extractor.h"
#include "support/temporary_directory.h"

#include <gtest/gtest.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

namespace CometEditor::Tests {
    TEST(ViewportTest, RebuildsAfterBackendsWereClosedDuringFailedRecreation) {
        Comet::Config config;
        config.vulkan.enable_validation = true;
        auto engine_result = Comet::Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto& renderer = engine.get_renderer();
        Comet::Tests::TemporaryDirectory directory;
        auto result = ImGuiContext::create(
            engine.get_window(), renderer.get_render_context(), directory.path() / "imgui.ini");
        ASSERT_TRUE(result) << result.error();
        auto& ui = *result.value();
        renderer.wait_idle();
        ui.release_swapchain_resources();
        // 模拟重建中后端已关闭，但后续 GPU 候选创建失败的状态。
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ui.release_swapchain_resources();
        auto rebuilt = ui.rebuild_swapchain_resources({.image_count_changed = true});
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        renderer.set_overlay_renderer([&](Comet::CommandBuffer& command) { ui.render(command); });
        {
            const auto preparation = renderer.prepare_frame();
            ASSERT_TRUE(preparation) << preparation.error();
            ASSERT_TRUE(preparation.value());
        }
        ASSERT_TRUE(ui.begin_frame());
        ui.end_frame();
        EXPECT_TRUE(renderer.render_frame({}));
        renderer.set_overlay_renderer({});
        renderer.wait_idle();
    }

    TEST(ViewportTest, RendersAcrossSceneChangesAndIgnoresPlayPicking) {
        Comet::Config config;
        config.window.width = 640;
        config.window.height = 480;
        config.vulkan.enable_validation = true;
        auto engine_result = Comet::Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto& renderer = engine.get_renderer();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({320, 240}));
        Comet::Tests::TemporaryDirectory directory;
        auto ui_result = ImGuiContext::create(
            engine.get_window(), renderer.get_render_context(), directory.path() / "imgui.ini");
        ASSERT_TRUE(ui_result) << ui_result.error();
        auto& ui = *ui_result.value();
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
        auto sampler = renderer.get_render_resources().get_sampler_manager().get_nearest_clamp();
        ASSERT_TRUE(sampler) << sampler.error();
        Viewport viewport(state, engine.get_scene_runtime(), selection, history, components, edit,
            shortcuts, renderer, engine.get_asset_registry(), ui, std::move(sampler).value());
        renderer.set_overlay_renderer(
            [&](Comet::CommandBuffer& command_buffer) { ui.render(command_buffer); });
        const auto draw_frame = [&] {
            engine.get_window().poll_events();
            const auto preparation = renderer.prepare_frame();
            EXPECT_TRUE(preparation);
            if(!preparation || !preparation.value())
                return false;
            viewport.update_texture();
            if(!ui.begin_frame())
                return false;
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({600, 400});
            viewport.panel().render();
            const auto updated = viewport.update(active_scene);
            ui.end_frame();
            EXPECT_TRUE(updated);
            if(!updated)
                return false;
            viewport.submit_feedback(active_scene);
            EXPECT_TRUE(renderer.render_frame(Comet::SceneExtractor::extract(*active_scene)));
            return true;
        };
        EXPECT_TRUE(draw_frame());
        EXPECT_TRUE(draw_frame());
        EXPECT_TRUE(viewport.panel().is_visible());

        auto invalid_rebuild = ui.rebuild_swapchain_resources({});
        EXPECT_FALSE(invalid_rebuild);
        renderer.get_render_context().wait_idle();
        ui.release_swapchain_resources();
        auto rebuilt = ui.rebuild_swapchain_resources({});
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        EXPECT_TRUE(draw_frame());

        renderer.get_render_context().wait_idle();
        ui.release_swapchain_resources();
        rebuilt = ui.rebuild_swapchain_resources({.image_count_changed = true});
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        EXPECT_TRUE(draw_frame());

        active_scene = &second_scene;
        selection.set_scene(second_scene);
        history.bind_scene(active_scene);
        const auto selected = second_scene.create_entity();
        viewport.apply_pick(Comet::ScenePickHit{.entity_id = selected.get_id()}, active_scene);
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

        renderer.set_overlay_renderer({});
        viewport.panel().cancel_interaction();
        renderer.get_render_context().wait_idle();
    }
}
#endif
