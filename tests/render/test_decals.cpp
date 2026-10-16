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
                        * glm::mat4_cast(glm::angleAxis(Math::PI / 2, Math::Vec3(1, 0, 0)))
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
}
