#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, OutlineMaskRespectsOcclusionFillAndRemovalAcrossSlots) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto mesh = lit_quad();
        auto black = std::make_shared<Material>("outlined object", "unlit_color");
        ASSERT_TRUE(black->set_vector_property("color", {0, 0, 0, 1}));
        auto green = std::make_shared<Material>("occluder", "unlit_color");
        ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, 1}));
        const Math::Vec2u size{65, 65};
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        struct Mode {
            RenderPath path;
            SampleCount samples;
            bool temporal;
        };
        for(const auto& mode : {Mode{RenderPath::Forward, SampleCount::Count1, false},
                Mode{RenderPath::Forward, SampleCount::Count4, false},
                Mode{RenderPath::Forward, SampleCount::Count1, true},
                Mode{RenderPath::Deferred, SampleCount::Count1, false}}) {
            SCOPED_TRACE(static_cast<int>(mode.path));
            SCOPED_TRACE(static_cast<int>(mode.samples));
            SCOPED_TRACE(mode.temporal);
            MaterialPrograms programs;
            auto scene = create_scene(programs, size, mode.samples, mode.temporal, mode.path);
            ASSERT_TRUE(scene);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            for(unsigned frame = 0; frame < 5; ++frame) {
                SCOPED_TRACE(frame);
                RenderWorld world;
                world.render_items.push_back({.entity_id = 1,
                    .model_matrix = Math::translate(Math::Mat4(1), {0, .4f, 0})
                                    * Math::scale(Math::Mat4(1), {.4f, .4f, 1}),
                    .mesh = mesh,
                    .material = {.resource = black},
                    .outline = {.enabled = frame != 4,
                        .color = {1, 0, 0, 1},
                        .width = 3,
                        .fill = frame == 1 || frame == 3 ? .5f : 0.f,
                        .through_walls = frame == 3}});
                if(frame == 2 || frame == 3)
                    world.render_items.push_back({.entity_id = 2,
                        .model_matrix = Math::translate(Math::Mat4(1), {0, .4f, .5f})
                                        * Math::scale(Math::Mat4(1), {.8f, .8f, 1}),
                        .mesh = mesh,
                        .material = {.resource = green}});
                ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                auto drawn = scene.value()->render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                auto image =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                auto pixels = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), 65 * 65 * 4);
                copy_output(frames, image->get_image(), pixels, size);
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                const auto bytes = pixels->read();
                const auto format = image->get_image()->get_info().format;
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                const auto channel = [&](int x, int y, unsigned c) {
                    return std::to_integer<int>(bytes[(y * 65 + x) * 4 + (bgra ? 2 - c : c)]);
                };
                const float expected_red = frame == 1 || frame == 3 ? .5f : 0.f;
                float expected_green = 0;
                if(frame == 2)
                    expected_green = 1;
                else if(frame == 3)
                    expected_green = .5f;
                EXPECT_NEAR(channel(32, 19, 0), mapped_byte(expected_red), 3);
                EXPECT_NEAR(channel(32, 19, 1), mapped_byte(expected_green), 3);
                EXPECT_LT(channel(32, 45, 0), 8);
                if(frame == 0 || frame == 1 || frame == 3)
                    EXPECT_GT(channel(46, 19, 0), 100);
                else
                    EXPECT_LT(channel(46, 19, 0), 8);
            }
        }
    }
}
