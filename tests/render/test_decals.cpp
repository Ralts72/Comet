#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, ProjectedDecalsReceiveLightRespectOcclusionAndPrecedeTransparency) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const auto mesh = lit_quad();
        auto black = std::make_shared<Material>("decal receiver", "unlit_color");
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        auto green = std::make_shared<Material>("foreground", "unlit_color");
        ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, 1}));
        auto glass = std::make_shared<Material>("glass", "unlit_color");
        ASSERT_TRUE(glass->set_vector_property("color", {0, 0, 1, .5f}));
        ASSERT_TRUE(glass->set_surface({.alpha_mode = AlphaMode::Blend}));
        const auto cutout = engine->get_render_resources().try_create_texture({.width = 2,
            .height = 2,
            .pixels = {
                255, 255, 255, 255, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255, 255}});
        ASSERT_TRUE(cutout);
        const Math::Vec2u size{65, 65};
        const auto camera = ViewProjectMatrix{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        const auto reference =
            Math::Vec3(pbr_reference({0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {1, 0, 0}, 0, 1, Math::PI));
        for(const auto samples : {SampleCount::Count1, SampleCount::Count4}) {
            SCOPED_TRACE(static_cast<unsigned>(samples));
            MaterialPrograms programs;
            auto scene = create_scene(programs, size, samples);
            ASSERT_TRUE(scene);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            for(unsigned frame = 0; frame < 9; ++frame) {
                SCOPED_TRACE(frame);
                RenderWorld world;
                world.render_items.push_back(
                    {.entity_id = 1, .mesh = mesh, .material = {.resource = black}});
                world.lights.push_back({.direction = {0, 0, -1}, .intensity = Math::PI});
                if(frame != 0 && frame != 3) {
                    const float x = frame == 2 ? .5f : 0;
                    const auto transform =
                        Math::translate(Math::Mat4(1), {x, 0, .5f})
                        * Math::to_mat4(Math::angle_axis(Math::PI / 2, Math::Vec3(1, 0, 0)))
                        * Math::scale(Math::Mat4(1), {.7f, .5f, .7f});
                    const auto bounds =
                        transform_box({Math::Vec3(-.5f), Math::Vec3(.5f)}, transform);
                    ASSERT_TRUE(bounds);
                    world.decals.push_back({.decal = {.entity_id = 9,
                                                .world_to_local = Math::inverse(transform),
                                                .bounds = *bounds,
                                                .color = {1, 0, 0, 1},
                                                .roughness = 1}});
                }
                if(frame == 4 || frame == 5) {
                    world.render_items.push_back({.entity_id = 2,
                        .model_matrix = Math::translate(Math::Mat4(1), {0, 0, .5f})
                                        * Math::scale(Math::Mat4(1), {.2f, .2f, 1}),
                        .mesh = mesh,
                        .material = {.resource = frame == 4 ? green : glass}});
                }
                if(frame == 6) {
                    auto blue = world.decals.front();
                    blue.decal.entity_id = 10;
                    blue.decal.draw_order = 0;
                    blue.decal.color = {0, 0, 1, 1};
                    world.decals.front().decal.draw_order = 1;
                    world.decals.push_back(blue); // 输入乱序时仍按覆盖顺序绘制。
                }
                if(frame == 7) {
                    world.decals.front().decal.texture = AssetHandle(12);
                    world.decals.front().texture = cutout.value();
                }
                if(frame == 8)
                    world.decals.front().decal.world_to_local =
                        Math::inverse(Math::translate(Math::Mat4(1), {0, 0, .5f})
                                      * Math::scale(Math::Mat4(1), {.7f, .7f, .5f}));
                ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                auto drawn = scene.value()->render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                const auto output =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                auto pixels = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 65 * 65 * 4);
                copy_output(frames, output->get_image(), pixels, size);
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                const auto bytes = pixels->read();
                const auto format = output->get_image()->get_info().format;
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                Math::Vec3 expected{0};
                if(frame == 1 || frame == 6)
                    expected = reference;
                if(frame == 4)
                    expected = {0, 1, 0};
                if(frame == 5)
                    expected = reference * .5f + Math::Vec3(0, 0, .5f);
                if(frame == 7)
                    expected = reference * .5f;
                for(unsigned channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(
                                    bytes[(32 * 65 + 32) * 4 + (bgra ? 2 - channel : channel)]),
                        mapped_byte(expected[channel]), 3)
                        << channel;
                if(frame == 2)
                    EXPECT_NEAR(std::to_integer<int>(bytes[(32 * 65 + 48) * 4 + (bgra ? 2 : 0)]),
                        mapped_byte(reference.r), 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, CameraDepthUsesColorLodAfterOffscreenReentry) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const auto high = lit_quad();
        auto low_data = MeshData{
            .vertices = {{{-.2f, -1, .5f}}, {{.2f, -1, .5f}}, {{.2f, 1, .5f}}, {{-.2f, 1, .5f}}},
            .indices = {0, 1, 2, 2, 3, 0}};
        auto low = engine->get_render_resources().try_create_mesh(low_data);
        ASSERT_TRUE(low);
        auto black = std::make_shared<Material>("LOD receiver", "unlit_color");
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        const Math::Vec2u size{65, 65};
        MaterialPrograms programs;
        auto scene = create_scene(programs, size);
        ASSERT_TRUE(scene);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        const auto projector = Math::translate(Math::Mat4(1), {.6f, 0, .5f})
                               * Math::to_mat4(Math::angle_axis(Math::PI / 2, Math::Vec3(1, 0, 0)))
                               * Math::scale(Math::Mat4(1), {.4f, .5f, .4f});
        const auto bounds = transform_box({Math::Vec3(-.5f), Math::Vec3(.5f)}, projector);
        ASSERT_TRUE(bounds);
        auto lod = std::make_shared<ResolvedMeshLod>();
        lod->settings.level1 = AssetHandle(2);
        lod->meshes[0] = low.value();
        lod->revisions[0] = 1;
        for(unsigned frame = 0; frame < 3; ++frame) {
            SCOPED_TRACE(frame);
            // 先选低模，离屏时转高模，再从迟滞区间返回。
            const float height = std::array{.2f, .4f, .25f}[frame];
            RenderWorld world{.scene_lifetime = 1};
            world.render_items.push_back({.entity_id = 1,
                .model_matrix = Math::translate(Math::Mat4(1), {frame == 1 ? 10.f : 0.f, 0, 0})
                                * Math::scale(Math::Mat4(1), {1, height, 1}),
                .mesh_handle = AssetHandle(1),
                .mesh_revision = 1,
                .mesh = high,
                .material = {.resource = black},
                .lod = lod});
            world.lights.push_back({.direction = {0, 0, -1}, .intensity = Math::PI});
            world.decals.push_back({.decal = {.entity_id = 9,
                                        .world_to_local = Math::inverse(projector),
                                        .bounds = *bounds,
                                        .color = {1, 0, 0, 1},
                                        .roughness = 1}});
            ASSERT_TRUE(scene.value()->prepare_material_programs(world));
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            auto drawn = scene.value()->render(frames, {world, camera}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            const auto output =
                scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
            auto pixels = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            copy_output(frames, output->get_image(), pixels, size);
            submit(device, frames, drawn.value());
            frames.wait_for_all_slots();
            const auto bytes = pixels->read();
            const auto format = output->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            const auto red = std::to_integer<int>(bytes[(32 * size.x + 52) * 4 + (bgra ? 2 : 0)]);
            if(frame == 2)
                EXPECT_GT(red, 100)
                    << "Main depth must cover the same high-LOD silhouette as color";
            else
                EXPECT_EQ(red, 0);
        }
    }

    TEST_F(RenderGraphGpuTest, DecalTogglesReuseFogAndBloomImagesWhenTemporalDepthExists) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs;
        const Math::Vec2u size{129, 97};
        auto scene = create_scene(programs, size, SampleCount::Count1, true);
        ASSERT_TRUE(scene);
        ASSERT_TRUE(scene.value()->prepare_post_process({.bloom_enabled = true}));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        RenderWorld world{.scene_lifetime = 1};
        world.environment.volumetric_fog = {.enabled = true, .density = .1f};
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        const auto allocated_bytes = [&] {
            uint64_t bytes = 0;
            for(const auto& heap : device.query_memory_budget().heaps)
                bytes += heap.allocation_bytes;
            return bytes;
        };
        for(unsigned frame = 0; frame < 8; ++frame) {
            SCOPED_TRACE(frame);
            world.decals.clear();
            if(frame == 4 || frame == 6)
                world.decals.push_back({.decal = {.entity_id = 9,
                                            .world_to_local = Math::Mat4(1),
                                            .bounds = {Math::Vec3(-.5f), Math::Vec3(.5f)},
                                            .color = {1, 0, 0, 1}}});
            RenderGeometry geometry;
            geometry.prepare(world);
            begin_frame(frames);
            const auto before = allocated_bytes();
            auto drawn = scene.value()->render(frames, {world, camera}, geometry);
            ASSERT_TRUE(drawn) << drawn.error();
            // 切换只增加贴花管线和描述符，无新增图像／buffer；旧槽仍持有已有附件。
            if(frame >= 4)
                EXPECT_EQ(allocated_bytes(), before);
            submit(device, frames, drawn.value());
            frames.wait_for_all_slots();
        }
    }
}
