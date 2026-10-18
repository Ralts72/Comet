#include "support/render_graph_gpu_fixture.h"
#include "render/passes/gpu_particle_simulation.h"
#include "render/passes/gpu_mesh_visibility.h"
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

    TEST_F(RenderGraphGpuTest, DynamicAlphaSortBoundsSmallBatchesAndSurvivesSceneRestart) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto depth = engine->get_render_resources().try_create_texture(
            TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(depth);
        GpuParticleSimulation simulation(device, 2);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 100}, {}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        unsigned frame = 0;
        for(const uint32_t count : {1u, 3u, 65u, 7u, 129u}) {
            SCOPED_TRACE(count);
            GpuParticleEmitter emitter;
            emitter.id = 7;
            emitter.settings.simulation = ParticleSimulation::GPU;
            emitter.settings.gpu_dynamics = true;
            emitter.settings.blend = ParticleBlend::Alpha;
            emitter.settings.capacity = 256;
            emitter.settings.speed = 0;
            emitter.settings.acceleration = {};
            emitter.settings.lifetime = 10;
            emitter.count = count;
            emitter.time = .2 + .01 * frame;
            const auto near_count = (count + 1) / 2;
            emitter.spawns = {{Math::translate(Math::Mat4(1), {0, 0, 1}), 0, 0, 0, near_count}};
            if(count > near_count)
                emitter.spawns.push_back({Math::translate(Math::Mat4(1), {0, 0, -1}), 0, 0,
                    near_count, count - near_count});
            begin_frame(frames);
            const auto prepared = simulation.prepare(frames, camera, std::span(&emitter, 1), true,
                depth.value()->get_image_view(), frame < 3 ? 1 : 2);
            ASSERT_TRUE(prepared) << prepared.error();
            const auto output = simulation.outputs().front();
            auto readback =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(),
                    sizeof(vk::DrawIndirectCommand) + count * sizeof(ParticleInstance));
            const auto command = frames.get_current_command_buffer().get();
            const vk::MemoryBarrier readable(
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer, {}, readable, {}, {});
            command.copyBuffer(output.indirect->get(), readback->get(),
                vk::BufferCopy(0, 0, sizeof(vk::DrawIndirectCommand)));
            command.copyBuffer(output.instances->get(), readback->get(),
                vk::BufferCopy(
                    0, sizeof(vk::DrawIndirectCommand), count * sizeof(ParticleInstance)));
            frames.retain_current_frame_resource(readback);
            submit(device, frames, {});
            frames.wait_for_all_slots();
            const auto bytes = readback->read();
            vk::DrawIndirectCommand draw;
            std::memcpy(&draw, bytes.data(), sizeof(draw));
            EXPECT_EQ(draw.vertexCount, 6u);
            EXPECT_EQ(draw.instanceCount, count);
            std::vector<ParticleInstance> instances(count);
            std::memcpy(
                instances.data(), bytes.data() + sizeof(draw), count * sizeof(ParticleInstance));
            for(size_t index = 1; index < instances.size(); ++index)
                EXPECT_LE(instances[index - 1].position_size.z, instances[index].position_size.z);
            ++frame;
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
    TEST_F(RenderGraphGpuTest, GpuMeshVisibilityCompactsPerBatchAndPublishesIndirectCounts) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        GpuMeshVisibility visibility(device, 2);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::vector<PushConstant> source;
        for(int i = 0; i < 512; ++i)
            source.push_back(
                {Math::translate(Math::Mat4(1), {float(i % 8) - 4, 0, -2}), {0, 0, 0, 0}});
        auto buffer =
            Buffer::try_create_cpu_buffer(device, Flags<BufferUsage>(BufferUsage::Storage),
                source.size() * sizeof(PushConstant), true, source.data());
        ASSERT_TRUE(buffer);
        const auto clip = Math::ortho(-1, 1, -1, 1, .1f, 10);
        const GpuMeshVisibility::Batch batch{{{-.1f, -.1f, -.1f}, {.1f, .1f, .1f}}, 0, 512, 36};
        for(int frame = 0; frame < 5; ++frame) {
            begin_frame(frames);
            auto prepared =
                visibility.prepare(frames, clip, buffer.value(), std::span(&batch, 1), 512);
            ASSERT_TRUE(prepared) << prepared.error();
            auto readback =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 20);
            const auto command = frames.get_current_command_buffer().get();
            const vk::MemoryBarrier readable(
                vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer, {}, readable, {}, {});
            command.copyBuffer(visibility.commands(frames.get_current_frame_slot_index())->get(),
                readback->get(), vk::BufferCopy(0, 0, 20));
            frames.retain_current_frame_resource(readback);
            submit(device, frames, {});
            frames.wait_for_all_slots();
            const auto bytes = readback->read();
            vk::DrawIndexedIndirectCommand draw;
            std::memcpy(&draw, bytes.data(), 20);
            EXPECT_EQ(draw.indexCount, 36u);
            EXPECT_EQ(draw.instanceCount, 192u);
            EXPECT_EQ(draw.firstInstance, 0u);
        }
    }

}
