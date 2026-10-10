#include "support/render_gpu_test.h"
#include "render/material/material.h"
#include "asset/data/mesh_data.h"
#include "asset/registry.h"
#include "render/resource/mesh.h"

namespace Comet::Tests {
    class QualityGpuTest: public RenderGpuTest {};

    TEST_F(QualityGpuTest, PublishesAtFrameBoundaryAndKeepsOutputResolution) {
        auto& renderer = engine->get_renderer();
        const Math::Vec2u output_size{101, 53};
        ASSERT_TRUE(prepare_offscreen_host(output_size));
        constexpr AssetHandle mesh_handle(9810), material_handle(9811);
        auto mesh = engine->get_render_resources().try_create_mesh(
            {.vertices = {{{-0.5f, -0.5f, -2}}, {{0.5f, -0.5f, -2}}, {{0, 0.5f, -2}}},
                .indices = {0, 1, 2}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(engine->get_asset_registry().register_asset(mesh_handle, mesh.value()));
        ASSERT_TRUE(engine->get_asset_registry().register_asset(
            material_handle, std::make_shared<Material>("quality", "unlit_color")));
        RenderScene scene;
        scene.cameras.push_back({.primary = true});
        scene.render_items.push_back(
            {.mesh_handle = mesh_handle, .material_handle = material_handle});
        const auto original = renderer.get_quality_settings();
        const auto prepare = [&] {
            const auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error().message;
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
        };
        for(const auto samples : renderer.supported_msaa_samples()) {
            prepare();
            ASSERT_FALSE(HasFatalFailure());
            const auto before = renderer.get_quality_settings();
            QualitySettings candidate{samples, renderer.max_anisotropy(), 0.5f};
            ASSERT_TRUE(renderer.request_quality_settings(candidate));
            EXPECT_EQ(renderer.get_quality_settings(), before);
            ASSERT_TRUE(renderer.render_frame(scene));
            prepare();
            ASSERT_FALSE(HasFatalFailure());
            EXPECT_EQ(renderer.get_quality_settings(), candidate);
            EXPECT_FALSE(renderer.quality_pending());
            EXPECT_TRUE(renderer.quality_error().empty());
            EXPECT_EQ(renderer.get_scene_size(), Math::Vec2u(51, 27));
            EXPECT_EQ(renderer.get_offscreen_frame().size, output_size);
            scene.post_process.bloom_enabled = true;
            ASSERT_TRUE(renderer.render_frame(scene));
            EXPECT_EQ(renderer.get_scene_renderer().get_material_statistics().draw_calls, 1u);
        }
        ASSERT_TRUE(renderer.request_quality_settings(original));
        prepare();
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_EQ(renderer.get_quality_settings(), original);
        EXPECT_EQ(renderer.get_scene_size(), output_size);
        ASSERT_TRUE(renderer.render_frame(scene));
        const auto view = renderer.get_scene_renderer().get_offscreen_color_view(0);
        EXPECT_FALSE(renderer.request_quality_settings({3, 8, 1}));
        EXPECT_FALSE(renderer.request_quality_settings({1, 8, 0}));
        EXPECT_EQ(renderer.get_scene_renderer().get_offscreen_color_view(0), view);
        auto clamped = renderer.resolve_quality_settings({1, 16, 1});
        ASSERT_TRUE(clamped);
        EXPECT_EQ(clamped.value().max_anisotropy, std::min(16.0f, renderer.max_anisotropy()));
        auto pending = original;
        pending.render_scale = 0.75f;
        ASSERT_TRUE(renderer.request_quality_settings(pending));
        EXPECT_TRUE(renderer.quality_pending());
        ASSERT_TRUE(renderer.request_quality_settings(original));
        EXPECT_FALSE(renderer.quality_pending());
        prepare();
        ASSERT_FALSE(HasFatalFailure());
        EXPECT_EQ(renderer.get_scene_renderer().get_offscreen_color_view(0), view);
        ASSERT_TRUE(renderer.render_frame(scene));
        renderer.prepare_shutdown();
        EXPECT_FALSE(renderer.request_quality_settings(original));
    }
}
