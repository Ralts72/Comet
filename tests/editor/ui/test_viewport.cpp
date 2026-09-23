#ifdef COMET_TEST_EDITOR_UI
#include "viewport/viewport.h"
#include "config/config.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "graphics/device.h"
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
#include "support/scene_motion_system.h"
#include "common/scope_exit.h"
#include "render/scene/scene_renderer.h"
#include "render/render_target.h"
#include "render/render_diagnostics.h"
#include "asset/registry.h"
#include "render/material/material.h"

#include <gtest/gtest.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <algorithm>

namespace CometEditor::Tests {
    TEST(ViewportTest, HiddenOffscreenViewSkipsGraphButKeepsRuntimeAndUiAlive) {
        Comet::Config config;
        config.window.width = 160;
        config.window.height = 120;
        config.vulkan.enable_validation = true;
        config.render.scene_output = Comet::Config::Render::SceneOutput::Offscreen;
        config.diagnostics.enable_render_diagnostics = true;
        auto created = Comet::Engine::create(config);
        ASSERT_TRUE(created) << created.error();
        auto& engine = *created.value();
        auto& renderer = engine.get_renderer();
        auto& scene_renderer = renderer.get_scene_renderer();
        ASSERT_TRUE(scene_renderer.is_offscreen());
        ASSERT_TRUE(scene_renderer.get_offscreen_color_view(0));
        const Comet::AssetHandle handle(72);
        auto material = std::make_shared<Comet::Material>("cached", "pbr");
        std::weak_ptr<Comet::Material> source = material;
        ASSERT_TRUE(engine.get_asset_registry().register_asset(handle, material));
        auto update = renderer.prepare_material_update(handle, material);
        ASSERT_TRUE(update) << update.error();
        std::move(update).value().publish();
        material.reset();
        engine.set_scene(std::make_unique<Comet::Scene>());
        auto calls = std::make_shared<Comet::Tests::RuntimeCalls>();
        ASSERT_TRUE(engine.add_system(std::make_unique<Comet::Tests::SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        Comet::Tests::TemporaryDirectory directory;
        auto ui_result = ImGuiContext::create(
            engine.get_window(), renderer.get_render_context(), directory.path() / "imgui.ini");
        ASSERT_TRUE(ui_result) << ui_result.error();
        auto& ui = *ui_result.value();
        const Comet::ScopeExit cleanup([&] {
            renderer.set_overlay_renderer({});
            renderer.set_viewport_pick_callback({});
            renderer.wait_idle();
        });
        unsigned overlays = 0;
        unsigned picks = 0;
        unsigned prepared_frames = 0;
        bool visible = true;
        renderer.set_viewport_pick_callback([&](auto) { ++picks; });
        renderer.set_overlay_renderer([&](Comet::CommandBuffer& command) {
            ++overlays;
            ui.render(command);
            const auto& snapshot = renderer.get_diagnostics().get_snapshot();
            EXPECT_EQ(snapshot.scene_rendered, visible);
            EXPECT_EQ(snapshot.cpu.has_value(), visible);
            const auto statistics = scene_renderer.get_material_statistics();
            EXPECT_EQ(statistics.frame_set_count, config.render.max_frames_in_flight);
            EXPECT_EQ(statistics.cached_material_versions, overlays < 3 ? 1u : 0u);
            EXPECT_EQ(source.expired(), overlays >= 3);
            if(!visible) {
                EXPECT_FALSE(snapshot.gpu);
                EXPECT_EQ(scene_renderer.get_material_statistics().draw_calls, 0);
                EXPECT_FLOAT_EQ(scene_renderer.get_post_process_settings().exposure, 1);
            }
            EXPECT_EQ(overlays, prepared_frames);
            EXPECT_EQ(calls->updates, prepared_frames);
            EXPECT_EQ(calls->starts, 1);
            EXPECT_EQ(calls->stops, 0);
            if(overlays == 4)
                engine.get_window().request_close();
        });
        unsigned attempts = 0;
        const auto run = engine.run(
            [&](Comet::UpdateContext) {
                if(overlays == 2)
                    EXPECT_TRUE(engine.get_asset_registry().unregister_asset(handle));
                if(++attempts > 10)
                    engine.get_window().request_close();
                return Comet::Result<void, Comet::Error>::success();
            },
            [&] {
                ++prepared_frames;
                visible = prepared_frames == 1 || prepared_frames == 4;
                if(!ui.begin_frame())
                    return Comet::Result<void, Comet::Error>::failure({"UI is not ready"});
                ImGui::Begin("UI stays active");
                ImGui::TextUnformatted("Inspector");
                ImGui::End();
                ui.end_frame();
                auto view = renderer.set_render_view({.visible = visible,
                    .camera_selection = Comet::RenderView::CameraSelection::Override,
                    .camera_override = Comet::RenderCamera{}});
                if(!view)
                    return Comet::Result<void, Comet::Error>::failure(view.error().as_error());
                if(!visible) {
                    renderer.request_viewport_pick({1, 1}, {160, 120});
                    EXPECT_TRUE(engine.get_scene()->set_post_process({.exposure = 2}));
                }
                return Comet::Result<void, Comet::Error>::success();
            });
        ASSERT_TRUE(run) << run.error();
        EXPECT_EQ(overlays, 4);
        EXPECT_EQ(picks, 0);
        EXPECT_FLOAT_EQ(scene_renderer.get_post_process_settings().exposure, 2);
    }

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
            shortcuts, renderer, engine.get_asset_registry(), ui, std::move(sampler).value(),
            std::min(
                renderer.get_render_context().get_device().get_capability().max_image_dimension_2d,
                std::uint32_t{4096}));
        renderer.set_overlay_renderer(
            [&](Comet::CommandBuffer& command_buffer) { ui.render(command_buffer); });
        const auto draw_frame = [&] {
            engine.get_window().poll_events();
            const auto preparation = renderer.prepare_frame();
            EXPECT_TRUE(preparation);
            if(!preparation || !preparation.value())
                return false;
            const auto frame = renderer.get_offscreen_frame();
            EXPECT_LT(frame.slot, renderer.get_frame_scheduler().get_frame_slot_count());
            EXPECT_EQ(frame.size, renderer.get_scene_renderer().get_render_target().get_size());
            EXPECT_EQ(frame.color_view,
                renderer.get_scene_renderer().get_offscreen_color_view(frame.slot));
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
