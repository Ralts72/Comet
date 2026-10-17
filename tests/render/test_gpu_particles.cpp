#include "support/render_graph_gpu_fixture.h"
#include "render/passes/gpu_particle_simulation.h"
#include "render/material/material_programs.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, GpuParticleBallisticsSortPaddingAndIndirectDrawSurviveSlotReuse) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        GpuParticleSimulation simulation(device, 2);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        ParticleEmitterComponent settings;
        settings.simulation = ParticleSimulation::GPU;
        settings.capacity = 8192;
        settings.rate = 0;
        settings.blend = ParticleBlend::Alpha;
        settings.spread = 0;
        const GpuParticleEmitter emitter{settings,
            {{Math::translate(Math::Mat4(1), {0, 0, 1}), 0, 0, 0, 3000},
                {Math::translate(Math::Mat4(1), {0, 0, -1}), .1, 0, 3000, 2001}},
            .5, {}, 5001};
        const ViewProjectMatrix camera{Math::Mat4(1), Math::Mat4(1)};
        std::vector<std::byte> previous;
        for(int frame = 0; frame < 5; ++frame) {
            SCOPED_TRACE(frame);
            auto current = emitter;
            if(frame == 4) {
                current.settings.capacity = current.count = 65536;
                current.settings.blend = ParticleBlend::Additive;
                current.spawns = {{Math::Mat4(1), 0, 0, 0, current.count}};
            }
            begin_frame(frames);
            auto prepared = simulation.prepare(frames, camera, std::span(&current, 1));
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(simulation.outputs().size(), 1u);
            const auto output = simulation.outputs()[0];
            const size_t bytes = current.count * sizeof(ParticleInstance);
            auto readback =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(),
                    bytes + sizeof(vk::DrawIndirectCommand));
            const auto command = frames.get_current_command_buffer().get();
            const vk::MemoryBarrier readable(
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer, {}, readable, {}, {});
            command.copyBuffer(
                output.instances->get(), readback->get(), vk::BufferCopy(0, 0, bytes));
            command.copyBuffer(output.indirect->get(), readback->get(),
                vk::BufferCopy(0, bytes, sizeof(vk::DrawIndirectCommand)));
            frames.retain_current_frame_resource(readback);
            if(frame == 3)
                simulation.reset();
            submit(device, frames, {});
            frames.wait_for_all_slots();
            const auto data = readback->read();
            if(data.size() == previous.size())
                EXPECT_EQ(data, previous);
            previous = data;
            vk::DrawIndirectCommand draw;
            std::memcpy(&draw, data.data() + bytes, sizeof(draw));
            EXPECT_EQ(draw.vertexCount, 6u);
            EXPECT_EQ(draw.instanceCount, current.count);
            EXPECT_EQ(draw.firstVertex, 0u);
            EXPECT_EQ(draw.firstInstance, 0u);
            std::vector<ParticleInstance> instances(current.count);
            std::memcpy(instances.data(), data.data(), bytes);
            EXPECT_NEAR(instances[0].position_size.y, frame == 4 ? .75 : .64, .0001);
            EXPECT_NEAR(instances[2001].position_size.y, .75, .0001);
            for(size_t i = 1; i < instances.size(); ++i) {
                ASSERT_TRUE(Math::is_finite(instances[i].position_size));
                EXPECT_LE(instances[i - 1].position_size.z, instances[i].position_size.z);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, GpuParticlesReachForwardAndDeferredColorWithoutCpuInstances) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const Math::Vec2u size{33, 33};
        auto snapshot = std::make_shared<ParticleSnapshot>();
        ParticleEmitterComponent settings;
        settings.simulation = ParticleSimulation::GPU;
        settings.speed = 0;
        settings.acceleration = {};
        settings.start_color = settings.end_color = {1, 0, 0, 1};
        settings.start_size = settings.end_size = 1;
        snapshot->gpu_emitters.push_back({settings, {{Math::Mat4(1), 0, 0, 0, 1}}, .25, {}, 1});
        RenderWorld world;
        world.particles = snapshot;
        world.particle_textures.resize(1);
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        for(auto path : {RenderPath::Forward, RenderPath::Deferred}) {
            MaterialPrograms programs;
            auto scene = create_scene(programs, size, SampleCount::Count1, false, path);
            ASSERT_TRUE(scene);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(2);
            FrameWait wait{device, frames};
            RenderGeometry geometry;
            geometry.prepare(world);
            for(int frame = 0; frame < 3; ++frame) {
                begin_frame(frames);
                auto drawn = scene.value()->render(frames, {world, camera}, geometry);
                ASSERT_TRUE(drawn) << drawn.error();
                const auto image =
                    scene.value()->get_offscreen_color_view(frames.get_current_frame_slot_index());
                auto readback = std::make_shared<Readback>(
                    device, context.get_context().get_physical_device(), size.x * size.y * 4);
                copy_output(frames, image->get_image(), readback, size);
                submit(device, frames, drawn.value());
                frames.wait_for_all_slots();
                const auto data = readback->read();
                const auto format = image->get_image()->get_info().format;
                const auto red =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM ? 2 : 0;
                EXPECT_NEAR(
                    std::to_integer<int>(data[(16 * 33 + 16) * 4 + red]), mapped_byte(1), 3);
            }
        }
    }
}
