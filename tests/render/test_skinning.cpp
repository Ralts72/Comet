#include "support/render_gpu_test.h"
#include "asset/data/mesh_data.h"
#include "render/material/material.h"
#include "asset/registry.h"
#include "render/resource/mesh.h"
#include "asset/import/mesh_importer.h"
#include <numeric>
namespace Comet::Tests {
    using SkinningGpuTest = RenderGpuTest;
    TEST_F(SkinningGpuTest, GpuPoseMovesPixelsAndSharedMeshInstancesKeepIndependentPoses) {
        const Math::Vec2u size{160, 120};
        ASSERT_TRUE(prepare_offscreen_host(size));
        auto skeleton = std::make_shared<AnimationData>();
        skeleton->mesh_node = 0;
        skeleton->nodes = {{"Mesh"}, {"Bone"}};
        skeleton->evaluation_order = {0, 1};
        skeleton->joints = {
            {.node = 1, .bounds = BoundingBox{{-0.25f, -0.25f, 0}, {0.25f, 0.25f, 0}}}};
        skeleton->clips = {{.name = "move",
            .duration = 1,
            .channels = {{.node = 1,
                .path = AnimationChannel::Path::Translation,
                .interpolation = AnimationChannel::Interpolation::Linear,
                .times = {0, 1},
                .values = {{-0.5f, 0, 0, 0}, {0.5f, 0, 0, 0}}}}}};
        MeshData data{.vertices = {{{-0.25f, -0.25f, 0}, {}, {0, 0, 1}},
                          {{0.25f, -0.25f, 0}, {}, {0, 0, 1}}, {{0, 0.25f, 0}, {}, {0, 0, 1}}},
            .indices = {0, 1, 2},
            .skin = std::vector<SkinInfluences>(3, {{0, 0, 0, 0}, {1, 0, 0, 0}}),
            .animation = skeleton};
        auto uploaded = engine->get_render_resources().try_create_mesh(data);
        ASSERT_TRUE(uploaded);
        auto material = std::make_shared<Material>("Animated", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {1, 0, 0, 1}));
        auto& assets = engine->get_asset_registry();
        ASSERT_TRUE(assets.register_asset(AssetHandle(10), uploaded.value()));
        ASSERT_TRUE(assets.register_asset(AssetHandle(11), material));
        RenderScene scene;
        scene.cameras.push_back({.primary = true,
            .view_matrix = Math::translate(Math::Mat4(1), {0, 0, -3}),
            .projection = RenderCamera::Projection::Orthographic,
            .orthographic_height = 2});
        scene.render_items.push_back(
            {.entity_id = 1, .mesh_handle = AssetHandle(10), .material_handle = AssetHandle(11)});
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler readback_frames(device, 1);
        readback_frames.initialize_swapchain_images(1);
        auto readback = std::make_shared<Readback>(device,
            engine->get_renderer().get_render_context().get_context().get_physical_device(),
            size.x * size.y * 4);
        const auto centroid = [&](double time) {
            scene.render_items[0].animation = std::make_shared<AnimationPlayback>(
                AnimationPlayback{.clip = "move", .time = time, .loop = false});
            render_and_copy(engine->get_renderer(), scene, readback_frames, readback);
            readback_frames.wait_for_all_slots();
            const auto pixels = readback->read();
            double total = 0;
            std::size_t count = 0;
            for(uint32_t y = 0; y < size.y; ++y)
                for(uint32_t x = 0; x < size.x; ++x) {
                    const auto index = (y * size.x + x) * 4;
                    if(std::max(std::to_integer<int>(pixels[index]),
                           std::to_integer<int>(pixels[index + 2]))
                            > 150
                        && std::to_integer<int>(pixels[index + 1]) < 40) {
                        total += x;
                        ++count;
                    }
                }
            EXPECT_GT(count, 50u);
            return count ? total / count : 0;
        };
        const double first = centroid(0);
        const double second = centroid(1);
        EXPECT_GT(second - first, 45);
        scene.render_items.push_back(scene.render_items.front());
        scene.render_items[1].entity_id = 2;
        scene.render_items[1].animation = std::make_shared<AnimationPlayback>(
            AnimationPlayback{.clip = "move", .time = 1, .loop = false});
        const double both = centroid(0);
        EXPECT_NEAR(both, (first + second) / 2, 1);
        scene.render_items.pop_back();
        EXPECT_NEAR(centroid(0), first, 1);
    }
}
