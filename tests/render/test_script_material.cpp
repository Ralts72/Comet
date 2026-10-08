#include "support/render_graph_gpu_fixture.h"
#include "asset/registry.h"
#include "core/project.h"
#include "input/input.h"
#include "render/scene/scene_extractor.h"
#include "scene/scene.h"
#include "scene/scene_runtime.h"
#include "scene/script_component.h"
#include "scene/systems/script_system.h"
#include "scripting/script.h"

#include <array>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, DemoLuaMaterialChangesReachPixelsAcrossPauseStepAndStop) {
        const auto project = Project::load(COMET_SAMPLE_PROJECT_DIRECTORY);
        ASSERT_TRUE(project) << project.error();
        const std::array<std::filesystem::path, 1> roots{"scripts/spin.lua"};
        auto scripts = Script::load_group(project.value().paths().assets(), roots);
        ASSERT_TRUE(scripts) << scripts.error().message;

        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(prepare_offscreen_host({4, 4}));
        auto& assets = engine->get_asset_registry();
        constexpr AssetHandle mesh_handle{9201}, material_handle{9202}, script_handle{9203};
        auto quad = lit_quad();
        ASSERT_TRUE(quad);
        ASSERT_TRUE(assets.register_asset(mesh_handle, quad));
        ASSERT_TRUE(assets.register_asset(script_handle, scripts.value().front()));
        const Math::Vec4 authored{0.5f, 0.25f, 0.1f, 1};
        auto material = std::make_shared<Material>("shared", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", authored));
        ASSERT_TRUE(material->set_scalar_property("metallic", 0));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        ASSERT_TRUE(assets.register_asset(material_handle, material));
        const auto revision = material->get_revision();
        MaterialPrograms materials(assets);

        Scene scene;
        ASSERT_TRUE(scene.set_environment({.background = false, .lighting = false}));
        ASSERT_TRUE(scene.set_post_process({.exposure = 1, .bloom_enabled = false}));
        auto target = scene.create_entity("ScriptTarget");
        target.set_transform({.translation = {-0.5f, 0, 0}, .scale = {0.5f, 1, 1}});
        target.add_component<MeshRendererComponent>(mesh_handle, material_handle);
        auto& script = target.add_component<ScriptComponent>();
        script.asset = script_handle;
        script.parameters["enabled"] = false;
        auto peer = scene.create_entity("SharedMaterialPeer");
        peer.set_transform({.translation = {0.5f, 0, 0}, .scale = {0.5f, 1, 1}});
        peer.add_component<MeshRendererComponent>(mesh_handle, material_handle);
        auto camera = scene.create_entity("Camera");
        camera.set_transform({.translation = {0, 0, 3}});
        camera.add_component<CameraComponent>(CameraComponent{.primary = true,
            .projection = CameraComponent::Projection::Orthographic,
            .orthographic_height = 2,
            .far_clip = 10});
        scene.create_entity("Light").add_component<LightComponent>(
            LightComponent{.intensity = Math::PI});

        SceneRuntime runtime;
        ASSERT_TRUE(runtime.set_input_actions(project.value().input_actions()));
        ASSERT_TRUE(
            runtime.add_system(std::make_unique<ScriptSystem>(ScriptAssets{assets}, &materials)));
        ASSERT_TRUE(runtime.start(scene));
        Input input;
        input.focus_event(true);

        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 6> outputs;
        const auto capture = [&](std::size_t index) {
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            render_and_copy(renderer, SceneExtractor::extract(scene), frames, outputs[index]);
        };
        ASSERT_NO_FATAL_FAILURE(capture(0));

        input.key_event(Input::Key::Tab, true);
        ASSERT_TRUE(runtime.advance(0, &input.publish_frame()));
        ASSERT_NO_FATAL_FAILURE(capture(1));
        input.key_event(Input::Key::Tab, false);
        ASSERT_TRUE(runtime.advance(0, &input.publish_frame()));
        input.key_event(Input::Key::Right, true);
        ASSERT_TRUE(runtime.advance(0, &input.publish_frame()));
        ASSERT_NO_FATAL_FAILURE(capture(2));

        ASSERT_TRUE(runtime.set_state(SceneRuntime::State::Paused));
        ASSERT_TRUE(scene.emit_event("demo.score_changed", 1.0f));
        ASSERT_TRUE(runtime.advance(1));
        ASSERT_NO_FATAL_FAILURE(capture(3));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        ASSERT_NO_FATAL_FAILURE(capture(4));
        EXPECT_FLOAT_EQ(target.get_component<TransformComponent>().translation.y, 0.4f);

        ASSERT_TRUE(runtime.stop());
        ASSERT_NO_FATAL_FAILURE(capture(5));
        EXPECT_FALSE(scene.get_material_overrides(target));
        EXPECT_EQ(material->get_vector_property("base_color"), authored);
        EXPECT_EQ(material->get_revision(), revision);

        // 所有阶段提交后再读回，避免把 Scene 值断言误当成 GPU 输出验证。
        frames.wait_for_all_slots();
        const auto view = renderer.get_scene_renderer().get_offscreen_color_view(0);
        const auto format = view->get_image()->get_info().format;
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const std::array<Math::Vec3, 6> expected_colors{Math::Vec3(authored), {0.2f, 0.55f, 1},
            {1, 0.5f, 0.1f}, {1, 0.5f, 0.1f}, {0.2f, 1, 0.25f}, Math::Vec3(authored)};
        for(std::size_t stage = 0; stage < outputs.size(); ++stage) {
            SCOPED_TRACE(stage);
            const auto bytes = outputs[stage]->read();
            for(std::size_t side = 0; side < 2; ++side) {
                SCOPED_TRACE(side);
                auto color = Math::Vec3(authored);
                if(side == 0)
                    color = expected_colors[stage];
                const auto expected = pbr_reference(
                    {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, glm::dvec3(color), 0, 1, Math::PI);
                const auto pixel = (2 * 4 + (side == 0 ? 0 : 3)) * 4;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel + component]),
                        mapped_byte(static_cast<float>(expected[channel])), 3);
                }
            }
        }
    }
}
