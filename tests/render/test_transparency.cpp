#include "support/render_graph_gpu_fixture.h"
#include "render/material/material_programs.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest,
        WeightedTransparencyCombinesMeshesCpuAndGpuParticlesWithoutOrderDependence) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto quad = lit_quad();
        auto red = std::make_shared<Material>("transparent PBR", "pbr");
        ASSERT_TRUE(red->set_vector_property("base_color", {1, 0, 0, .5f}));
        ASSERT_TRUE(red->set_vector_property("emissive_color", {1, 0, 0, 1}));
        ASSERT_TRUE(red->set_scalar_property("emissive_strength", 1));
        ASSERT_TRUE(red->set_surface({.alpha_mode = AlphaMode::Blend, .double_sided = true}));
        auto blue = std::make_shared<Material>("transparent unlit", "unlit_color");
        ASSERT_TRUE(blue->set_vector_property("color", {0, 0, 1, .5f}));
        ASSERT_TRUE(blue->set_surface({.alpha_mode = AlphaMode::Blend, .double_sided = true}));
        auto green = std::make_shared<Material>("opaque", "unlit_color");
        ASSERT_TRUE(green->set_vector_property("color", {0, 1, 0, 1}));
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        struct Mode {
            RenderPath path;
            SampleCount samples;
            bool temporal;
        };
        const Mode modes[]{{RenderPath::Forward, SampleCount::Count1, false},
            {RenderPath::Forward, SampleCount::Count4, false},
            {RenderPath::Forward, SampleCount::Count1, true},
            {RenderPath::Deferred, SampleCount::Count1, false},
            {RenderPath::Deferred, SampleCount::Count1, true}};
        for(const auto& mode : modes) {
            MaterialPrograms programs;
            auto scene = create_scene(programs, {65, 65}, mode.samples, mode.temporal, mode.path);
            ASSERT_TRUE(scene);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            std::vector<std::byte> reference;
            for(unsigned frame = 0; frame < 5; ++frame) {
                SCOPED_TRACE(frame);
                const Math::Vec2u size = frame >= 3 ? Math::Vec2u(33) : Math::Vec2u(65);
                if(frame == 3)
                    ASSERT_TRUE(scene.value()->resize_offscreen_target(size));
                PostProcessSettings post;
                post.weighted_oit = frame != 2;
                ASSERT_TRUE(scene.value()->prepare_post_process(post));
                auto particles = std::make_shared<ParticleSnapshot>();
                particles->instances = {
                    {{0, 0, 0, 1}, {1, 0, 0, .25f}}, {{0, 0, 0, 1}, {0, 0, 1, .25f}}};
                if(frame == 1)
                    std::ranges::reverse(particles->instances);
                particles->batches = {{{}, 0, 2, ParticleBlend::Alpha}};
                ParticleEmitterComponent emitter;
                emitter.simulation = ParticleSimulation::GPU;
                emitter.gpu_dynamics = frame == 3;
                emitter.blend = ParticleBlend::Alpha;
                emitter.speed = 0;
                emitter.acceleration = {};
                emitter.start_color = emitter.end_color = {0, 1, 0, .25f};
                emitter.start_size = emitter.end_size = 1;
                particles->gpu_emitters = {{emitter, {{Math::Mat4(1), 0, 0, 0, 1}}, .25, {}, 1}};
                RenderWorld world;
                world.particles = particles;
                world.particle_textures.resize(2);
                world.render_items = {{.entity_id = 1,
                                          .mesh = quad,
                                          .material = {.resource = frame == 1 ? blue : red}},
                    {.entity_id = 2,
                        .mesh = quad,
                        .material = {.resource = frame == 1 ? red : blue}},
                    {.entity_id = 3,
                        .model_matrix = Math::translate(Math::Mat4(1), {-.65f, 0, .5f})
                                        * Math::scale(Math::Mat4(1), {.25f, 1, 1}),
                        .mesh = quad,
                        .material = {.resource = green}}};
                if(frame == 4) {
                    world.render_items.clear();
                    particles->instances = {{{0, 0, 0, 1}, {1, 1, 1, .25f}}};
                    particles->batches = {{{}, 0, 1, ParticleBlend::Additive}};
                    particles->gpu_emitters.clear();
                    world.particle_textures.resize(1);
                }
                ASSERT_TRUE(scene.value()->prepare_material_programs(world));
                RenderGeometry geometry;
                geometry.prepare(world);
                begin_frame(frames);
                auto drawn = scene.value()->render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                const auto image =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                auto pixels = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), size.x * size.y * 4);
                copy_output(frames, image->get_image(), pixels, size);
                std::shared_ptr<Readback> hdr;
                if(frame == 4 && mode.temporal) {
                    hdr = std::make_shared<Readback>(
                        device, context.get_context().get_physical_device(), size.x * size.y * 8);
                    const auto image =
                        scene.value()->get_hdr_color_view(frames.get_current_frame_slot_index());
                    copy_output(frames, image->get_image(), hdr, size);
                }
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                if(hdr) {
                    const auto bytes = hdr->read();
                    uint16_t alpha = 0;
                    const size_t offset = ((size_t(size.y / 2) * size.x + size.x / 2) * 4 + 3) * 2;
                    std::memcpy(&alpha, bytes.data() + offset, sizeof(alpha));
                    EXPECT_GT(Math::unpack_half(alpha), .1f);
                    continue;
                }
                const auto bytes = pixels->read();
                if(frame == 4) {
                    const size_t center = (size_t(size.y / 2) * size.x + size.x / 2) * 4;
                    EXPECT_GT(std::to_integer<int>(bytes[center]), 80);
                    continue;
                }
                const auto format = image->get_image()->get_info().format;
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                const auto channel = [&](uint32_t x, uint32_t y, uint32_t c) {
                    return std::to_integer<int>(bytes[(y * size.x + x) * 4 + (bgra ? 2 - c : c)]);
                };
                EXPECT_GT(channel(size.x / 2, size.y / 2, 0), 50);
                EXPECT_GT(channel(size.x / 2, size.y / 2, 1), 20);
                EXPECT_GT(channel(size.x / 2, size.y / 2, 2), 50);
                EXPECT_NEAR(channel(size.x / 6, size.y / 2, 1), mapped_byte(1), 3);
                EXPECT_LT(channel(size.x / 6, size.y / 2, 0), 8);
                if(frame == 0)
                    reference = bytes;
                else if(frame == 1 && !mode.temporal) {
                    ASSERT_EQ(reference.size(), bytes.size());
                    for(size_t i = 0; i < bytes.size(); ++i)
                        ASSERT_LE(std::abs(std::to_integer<int>(bytes[i])
                                           - std::to_integer<int>(reference[i])),
                            2);
                }
            }
        }
    }
}
