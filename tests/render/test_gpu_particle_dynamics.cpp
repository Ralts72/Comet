#include "support/render_graph_gpu_fixture.h"
#include "render/passes/gpu_particle_simulation.h"
#include "render/resource/vector_field.h"
#include "render/particle_collision_scene.h"

namespace Comet::Tests {
    TEST(ParticleCollisionTest, StaticGeometryCachesVersionsAndUsesTheSharedBvhLayout) {
        ParticleCollisionBuilder builder;
        MeshData mesh;
        mesh.vertices = {{{-2, 0, -2}}, {{2, 0, -2}}, {{0, 0, 2}}};
        const ParticleMeshCollider collider{
            1, AssetHandle{77}, Math::translate(Math::Mat4(1), {0, 2, 0}), 4};
        uint32_t reads = 0;
        uint64_t revision = 1;
        const auto read = [&](AssetHandle) {
            ++reads;
            return Result<MeshData, Error>::success(mesh);
        };
        auto first =
            builder.update(1, std::span(&collider, 1), read, [&](AssetHandle) { return revision; });
        ASSERT_TRUE(first) << first.error().message;
        ASSERT_EQ(first.value()->geometry->triangles.size(), 1u);
        EXPECT_EQ(first.value()->geometry->triangles[0].surface[0], 4u);
        EXPECT_EQ(first.value()->geometry->triangles[0].positions[0].y, 2);
        EXPECT_EQ(first.value()->geometry->nodes[0].links[3], 1u);
        auto stable =
            builder.update(1, std::span(&collider, 1), read, [&](AssetHandle) { return revision; });
        ASSERT_TRUE(stable);
        EXPECT_EQ(stable.value(), first.value());
        EXPECT_EQ(reads, 1u);
        ++revision;
        mesh.vertices[0].position.y = 1;
        auto changed =
            builder.update(1, std::span(&collider, 1), read, [&](AssetHandle) { return revision; });
        ASSERT_TRUE(changed);
        EXPECT_NE(changed.value(), first.value());
        EXPECT_EQ(changed.value()->geometry->triangles[0].positions[0].y, 3);
        EXPECT_EQ(first.value()->geometry->triangles[0].positions[0].y, 2);
        EXPECT_EQ(reads, 2u);
        mesh.indices = {0, 1, 9};
        ++revision;
        EXPECT_FALSE(builder.update(
            1, std::span(&collider, 1), read, [&](AssetHandle) { return revision; }));
    }

    TEST(ParticleCollisionTest, DeformedGeometryRefitsAndKeepsSubmittedSnapshots) {
        MeshData mesh;
        mesh.vertices = {{{-1, 0, 0}}, {{1, 0, 0}}, {{0, 2, 0}}};
        ParticleMeshCollider collider{1, AssetHandle{77}, Math::Mat4(1), 4};
        ParticleCollisionBuilder builder;
        unsigned reads = 0;
        const auto update = [&] {
            return builder.update(1, std::span(&collider, 1), [&](AssetHandle) {
                ++reads;
                return Result<MeshData, Error>::success(mesh);
            });
        };
        const auto first = update();
        ASSERT_TRUE(first) << first.error().message;
        collider.bend = {.3f, 0, 2, 0};
        collider.model = Math::translate(Math::Mat4(1), {0, 1, 0});
        const auto bent = update();
        ASSERT_TRUE(bent) << bent.error().message;
        EXPECT_EQ(reads, 1u);
        EXPECT_EQ(bent.value()->geometry->nodes[0].links, first.value()->geometry->nodes[0].links);
        EXPECT_GT(bent.value()->geometry->triangles[0].positions[2].x, 0);
        EXPECT_EQ(first.value()->geometry->triangles[0].positions[2], Math::Vec4(0, 2, 0, 1));
        EXPECT_EQ(update().value(), bent.value());
        collider.bend = {};
        collider.model = Math::Mat4(1);
        EXPECT_EQ(update().value()->geometry->triangles[0].positions[2], Math::Vec4(0, 2, 0, 1));
    }

    TEST_F(RenderGraphGpuTest, GpuMeshSweepHitsFacesEdgesAndVerticesAndPreservesPausedState) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const ViewProjectMatrix camera{Math::look_at({100, 100, 100}, {101, 100, 100}, {0, 1, 0}),
            Math::ortho(-1, 1, -1, 1, .1f, 10)};
        auto depth = engine->get_render_resources().try_create_texture(
            TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(depth);
        MeshData mesh;
        mesh.vertices = {{{-2, 0, -2}}, {{2, 0, -2}}, {{0, 0, 2}}};
        ParticleCollisionBuilder builder;
        const ParticleMeshCollider collider{1, AssetHandle{77}, Math::Mat4(1), 4};
        auto collision = builder.update(1, std::span(&collider, 1),
            [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); });
        ASSERT_TRUE(collision);
        auto crowded_mesh = mesh;
        crowded_mesh.indices.clear();
        for(int i = 0; i < 128; ++i)
            crowded_mesh.indices.insert(crowded_mesh.indices.end(), {0, 1, 2});
        ParticleCollisionBuilder crowded_builder;
        auto crowded_collision = crowded_builder.update(1, std::span(&collider, 1),
            [&](AssetHandle) { return Result<MeshData, Error>::success(crowded_mesh); });
        ASSERT_TRUE(crowded_collision);
        struct State {
            Math::Vec4 position_age, velocity;
            std::array<uint32_t, 4> metadata;
        };
        for(auto position :
            {Math::Vec3(0, 1, 0), Math::Vec3(0, 1, -2.04f), Math::Vec3(2.04f, 1, -2.04f)})
            for(auto mode : {ParticleCollision::Bounce, ParticleCollision::Kill})
                for(bool layers : {true, false})
                    for(bool crowded : {false, true}) {
                        SCOPED_TRACE(position.x);
                        SCOPED_TRACE(position.z);
                        SCOPED_TRACE(int(mode));
                        SCOPED_TRACE(layers);
                        SCOPED_TRACE(crowded);
                        GpuParticleSimulation simulation(device, 2);
                        FrameScheduler frames(device, 2);
                        frames.initialize_swapchain_images(2);
                        FrameWait wait{device, frames};
                        GpuParticleEmitter emitter;
                        emitter.id = 21;
                        emitter.count = 1;
                        emitter.time = .15;
                        emitter.settings.simulation = ParticleSimulation::GPU;
                        emitter.settings.gpu_dynamics = true;
                        emitter.settings.gpu_world_collision = true;
                        emitter.settings.collision = mode;
                        emitter.settings.collision_layers = layers ? 4 : 2;
                        emitter.settings.collision_radius = .08f;
                        emitter.settings.mesh_collision_budget = crowded ? 16 : 256;
                        emitter.settings.restitution = 1;
                        emitter.settings.speed = 0;
                        emitter.settings.lifetime = 2;
                        emitter.settings.acceleration = {0, -200, 0};
                        emitter.spawns = {{Math::translate(Math::Mat4(1), position), 0, 0, 0, 1}};
                        std::vector<std::byte> previous;
                        for(int frame = 0; frame < 2; ++frame) {
                            begin_frame(frames);
                            const auto ready = simulation.prepare(frames, camera,
                                std::span(&emitter, 1), false, depth.value()->get_image_view(), 1,
                                {}, {}, crowded ? crowded_collision.value() : collision.value());
                            ASSERT_TRUE(ready) << ready.error();
                            auto readback = std::make_shared<Readback>(device,
                                context.get_context().get_physical_device(),
                                sizeof(vk::DrawIndirectCommand) + sizeof(State));
                            auto command = frames.get_current_command_buffer().get();
                            const vk::MemoryBarrier readable(vk::AccessFlagBits::eShaderWrite,
                                vk::AccessFlagBits::eTransferRead);
                            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                                vk::PipelineStageFlagBits::eTransfer, {}, readable, {}, {});
                            command.copyBuffer(simulation.outputs().front().indirect->get(),
                                readback->get(), vk::BufferCopy(0, 0, readback->get_size()));
                            frames.retain_current_frame_resource(readback);
                            std::vector<QueueSemaphoreSubmit> waits;
                            if(const auto upload = depth.value()->get_ready_completion();
                                upload.is_valid())
                                waits.emplace_back(
                                    upload, Flags<PipelineStage>(PipelineStage::ComputeShader));
                            submit(device, frames, waits);
                            frames.wait_for_all_slots();
                            const auto bytes = readback->read();
                            State state;
                            vk::DrawIndirectCommand draw;
                            std::memcpy(&draw, bytes.data(), sizeof(draw));
                            std::memcpy(&state, bytes.data() + sizeof(draw), sizeof(state));
                            EXPECT_TRUE(Math::is_finite(state.position_age));
                            if(layers && (mode == ParticleCollision::Kill || crowded))
                                EXPECT_EQ(draw.instanceCount, 0u);
                            else {
                                EXPECT_EQ(draw.instanceCount, 1u);
                                if(layers)
                                    EXPECT_GT(state.position_age.y, -.2f);
                                else
                                    EXPECT_NEAR(state.position_age.y, -1.25f, .001);
                            }
                            if(frame)
                                EXPECT_EQ(bytes, previous);
                            previous = bytes;
                        }
                    }
    }

    TEST_F(RenderGraphGpuTest, GpuDynamicsKeepsVelocityPauseAndScreenCollisionAcrossFrameSlots) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        const auto clip = camera.projection * camera.view * Math::Vec4(0, 0, 0, 1);
        const float surface_depth = clip.z / clip.w;
        TextureData data{.width = 8, .height = 8, .format = Format::R16G16B16A16_SFLOAT};
        data.pixels.resize(8 * 8 * 4 * sizeof(uint16_t));
        const auto encoded_depth = Math::pack_half(surface_depth);
        for(size_t i = 0; i < 64 * 4; ++i)
            std::memcpy(
                data.pixels.data() + i * sizeof(uint16_t), &encoded_depth, sizeof(uint16_t));
        auto depth = engine->get_render_resources().try_create_texture(std::move(data));
        ASSERT_TRUE(depth) << depth.error().message;
        struct State {
            Math::Vec4 position_age, velocity;
            std::array<uint32_t, 4> metadata;
        };
        static_assert(sizeof(State) == 48);
        for(uint32_t slots : {1u, 2u})
            for(auto collision :
                {ParticleCollision::None, ParticleCollision::Bounce, ParticleCollision::Kill}) {
                SCOPED_TRACE(slots);
                SCOPED_TRACE(static_cast<int>(collision));
                GpuParticleSimulation simulation(device, slots);
                FrameScheduler frames(device, slots);
                frames.initialize_swapchain_images(slots);
                FrameWait wait{device, frames};
                GpuParticleEmitter emitter;
                emitter.id = 21;
                emitter.count = 513;
                emitter.settings.simulation = ParticleSimulation::GPU;
                emitter.settings.gpu_dynamics = true;
                emitter.settings.blend = ParticleBlend::Alpha;
                emitter.settings.capacity = 1024;
                emitter.settings.collision = collision;
                emitter.settings.collision_radius = .01f;
                emitter.settings.restitution = .75f;
                emitter.settings.lifetime = 4;
                emitter.settings.speed = 0;
                emitter.settings.acceleration = {0, 0, -20};
                emitter.settings.turbulence = collision == ParticleCollision::None ? 2.f : 0.f;
                emitter.spawns = {
                    {Math::translate(Math::Mat4(1), {0, 0, 1}), 0, 0, 0, emitter.count}};
                std::vector<std::byte> paused, restarted;
                const std::array times{.1, .2, .2, .3, .4, .5, .2, .4, .4, .4};
                for(size_t frame = 0; frame < times.size(); ++frame) {
                    SCOPED_TRACE(frame);
                    emitter.time = times[frame];
                    if(frame == 6) {
                        emitter.settings.capacity = emitter.count = 65536;
                        emitter.spawns.front().count = emitter.count;
                    }
                    if(frame >= 7) {
                        emitter.id = frame == 7 ? 99 : 21;
                        emitter.count = emitter.spawns.front().count = 513;
                    }
                    begin_frame(frames);
                    auto result = simulation.prepare(frames, camera, std::span(&emitter, 1),
                        frame == 3, depth.value()->get_image_view(), frame >= 6 ? 2 : 1);
                    ASSERT_TRUE(result) << result.error();
                    const auto& output = simulation.outputs().front();
                    auto readback = std::make_shared<Readback>(device,
                        context.get_context().get_physical_device(),
                        sizeof(vk::DrawIndirectCommand) + sizeof(State));
                    auto command = frames.get_current_command_buffer().get();
                    const vk::MemoryBarrier readable(
                        vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead);
                    command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, {}, readable, {}, {});
                    command.copyBuffer(output.indirect->get(), readback->get(),
                        vk::BufferCopy(0, 0, readback->get_size()));
                    frames.retain_current_frame_resource(readback);
                    std::vector<QueueSemaphoreSubmit> waits;
                    const auto ready = depth.value()->get_ready_completion();
                    if(ready.is_valid())
                        waits.emplace_back(
                            ready, Flags<PipelineStage>(PipelineStage::ComputeShader));
                    submit(device, frames, waits);
                    frames.wait_for_all_slots();
                    const auto bytes = readback->read();
                    vk::DrawIndirectCommand draw;
                    State state;
                    std::memcpy(&draw, bytes.data(), sizeof(draw));
                    std::memcpy(&state, bytes.data() + sizeof(draw), sizeof(state));
                    EXPECT_EQ(draw.vertexCount, 6u);
                    EXPECT_TRUE(Math::is_finite(state.position_age));
                    EXPECT_TRUE(Math::is_finite(state.velocity));
                    if(frame == 1)
                        paused = bytes;
                    if(frame == 2)
                        EXPECT_EQ(bytes, paused);
                    if(frame == 7)
                        restarted = bytes;
                    if(frame >= 8)
                        EXPECT_EQ(bytes, restarted);
                    if(collision == ParticleCollision::Kill && frame >= 4 && frame != 6) {
                        EXPECT_EQ(draw.instanceCount, 0u);
                        EXPECT_EQ(state.metadata[2], 0u);
                    } else
                        EXPECT_EQ(draw.instanceCount, emitter.count);
                    if(collision == ParticleCollision::Bounce && frame == 4) {
                        EXPECT_GT(state.position_age.z, 0);
                        EXPECT_GT(state.velocity.z, 0);
                    }
                    if(collision == ParticleCollision::None && frame == 5)
                        EXPECT_GT(Math::length(Math::Vec2(state.position_age)), .001);
                    if(collision != ParticleCollision::None && frame == 6)
                        EXPECT_NEAR(state.position_age.z, .6f, .001);
                }
            }
    }
    TEST_F(RenderGraphGpuTest, GpuWorldCollisionAndFieldsWorkOutsideTheCameraAndHonorLayers) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        const ViewProjectMatrix camera{Math::look_at({100, 100, 100}, {101, 100, 100}, {0, 1, 0}),
            Math::ortho(-1, 1, -1, 1, .1f, 10)};
        TextureData data{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}};
        auto depth = engine->get_render_resources().try_create_texture(std::move(data));
        ASSERT_TRUE(depth);
        struct State {
            Math::Vec4 position_age, velocity;
            std::array<uint32_t, 4> metadata;
        };
        for(int shape : {0, 1, 2})
            for(auto mode :
                {ParticleCollision::None, ParticleCollision::Bounce, ParticleCollision::Kill})
                for(bool layers : {false, true}) {
                    SCOPED_TRACE(shape);
                    SCOPED_TRACE(int(mode));
                    SCOPED_TRACE(layers);
                    GpuParticleSimulation simulation(device, 1);
                    FrameScheduler frames(device, 1);
                    frames.initialize_swapchain_images(1);
                    FrameWait wait{device, frames};
                    ParticleWorldCollider collider;
                    collider.world_to_local =
                        Math::translate(Math::Mat4(1), {0, shape ? 0.f : .5f, 0});
                    collider.extent_shape =
                        shape ? Math::Vec4(1, shape == 2 ? .5f : 1.f, 1, float(shape))
                              : Math::Vec4(10, .5f, 10, 0);
                    GpuParticleEmitter emitter;
                    emitter.id = 1;
                    emitter.count = 1;
                    emitter.settings.simulation = ParticleSimulation::GPU;
                    emitter.settings.gpu_dynamics = true;
                    emitter.settings.gpu_world_collision = true;
                    emitter.settings.capacity = 1;
                    emitter.settings.speed = 0;
                    emitter.settings.lifetime = 4;
                    emitter.settings.acceleration = {0, -20, 0};
                    emitter.settings.wind = {1, 0, 0};
                    emitter.settings.vortex_strength = 2;
                    emitter.settings.vortex_center = {-.5f, 2, 0};
                    emitter.settings.collision = mode;
                    emitter.settings.collision_layers = layers ? 1 : 2;
                    emitter.settings.collision_radius = .01f;
                    emitter.settings.restitution = .75f;
                    emitter.spawns = {{Math::translate(Math::Mat4(1), {0, 2, 0}), 0, 0, 0, 1}};
                    std::vector<std::byte> previous;
                    bool bounced = false;
                    for(int frame = 0; frame <= 11; ++frame) {
                        emitter.time = std::min(frame, 10) * .05;
                        begin_frame(frames);
                        auto prepared = simulation.prepare(frames, camera, std::span(&emitter, 1),
                            false, depth.value()->get_image_view(), 1, std::span(&collider, 1));
                        ASSERT_TRUE(prepared) << prepared.error();
                        auto readback = std::make_shared<Readback>(device,
                            context.get_context().get_physical_device(),
                            sizeof(vk::DrawIndirectCommand) + sizeof(State));
                        auto command = frames.get_current_command_buffer().get();
                        command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eTransfer, {},
                            vk::MemoryBarrier(vk::AccessFlagBits::eShaderWrite,
                                vk::AccessFlagBits::eTransferRead),
                            {}, {});
                        command.copyBuffer(simulation.outputs().front().indirect->get(),
                            readback->get(), vk::BufferCopy(0, 0, readback->get_size()));
                        frames.retain_current_frame_resource(readback);
                        std::vector<QueueSemaphoreSubmit> waits;
                        if(auto ready = depth.value()->get_ready_completion(); ready.is_valid())
                            waits.emplace_back(
                                ready, Flags<PipelineStage>(PipelineStage::ComputeShader));
                        submit(device, frames, waits);
                        frames.wait_for_all_slots();
                        const auto bytes = readback->read();
                        State state;
                        std::memcpy(
                            &state, bytes.data() + sizeof(vk::DrawIndirectCommand), sizeof(state));
                        bounced |= state.velocity.y > 0;
                        if(frame == 10)
                            previous = bytes;
                        if(frame != 11)
                            continue;
                        EXPECT_EQ(bytes, previous);
                        vk::DrawIndirectCommand draw;
                        std::memcpy(&draw, bytes.data(), sizeof(draw));
                        std::memcpy(&state, bytes.data() + sizeof(draw), sizeof(state));
                        EXPECT_TRUE(Math::is_finite(state.position_age));
                        EXPECT_TRUE(Math::is_finite(state.velocity));
                        EXPECT_GT(state.position_age.x, 0);
                        EXPECT_LT(state.position_age.z, 0);
                        if(!layers || mode == ParticleCollision::None)
                            EXPECT_LT(state.position_age.y, 0);
                        else if(mode == ParticleCollision::Kill)
                            EXPECT_EQ(draw.instanceCount, 0u);
                        else {
                            EXPECT_EQ(draw.instanceCount, 1u);
                            EXPECT_GT(state.position_age.y, shape ? 1.f : 0.f);
                            EXPECT_TRUE(bounced);
                        }
                    }
                }
    }

    TEST_F(RenderGraphGpuTest, ImportedVectorFieldMatchesTrilinearReferenceAndRetainsPausedState) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        VectorFieldData data;
        data.bounds = {{-2, -2, -2}, {2, 2, 2}};
        for(int z = 0; z < 2; ++z)
            for(int y = 0; y < 2; ++y)
                for(int x = 0; x < 2; ++x)
                    data.values.emplace_back(1.f + y, 2.f - x, .5f + z);
        auto field = VectorField::try_create(engine->get_render_resources(), data);
        ASSERT_TRUE(field);
        auto depth = engine->get_render_resources().try_create_texture(
            TextureData{.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(depth);
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        GpuParticleEmitter emitter;
        emitter.count = 1;
        emitter.id = 1;
        auto& settings = emitter.settings;
        settings.simulation = ParticleSimulation::GPU;
        settings.gpu_dynamics = true;
        settings.vector_field = AssetHandle(1);
        settings.speed = 0;
        settings.acceleration = {0, 0, 0};
        settings.lifetime = 4;
        const Math::Vec3 origin(.3f, .2f, -.1f);
        emitter.spawns = {{Math::translate(Math::Mat4(1), origin), 0, 0, 0, 1}};
        GpuParticleSimulation simulation(device, 2);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<const VectorField>, 1> fields{field.value()};
        struct State {
            Math::Vec4 position_age, velocity;
            std::array<uint32_t, 4> metadata;
        };
        Math::Vec3 expected = origin, velocity(0);
        std::vector<std::byte> previous;
        for(int frame = 0; frame < 4; ++frame) {
            emitter.time = frame ? .1 : 0;
            begin_frame(frames);
            auto prepared = simulation.prepare(frames, camera, std::span(&emitter, 1), false,
                depth.value()->get_image_view(), 1, {}, fields);
            ASSERT_TRUE(prepared) << prepared.error();
            auto readback =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(),
                    sizeof(vk::DrawIndirectCommand) + sizeof(State));
            auto command = frames.get_current_command_buffer().get();
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer, {},
                vk::MemoryBarrier(
                    vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead),
                {}, {});
            command.copyBuffer(simulation.outputs().front().indirect->get(), readback->get(),
                vk::BufferCopy(0, 0, readback->get_size()));
            frames.retain_current_frame_resource(readback);
            std::vector<QueueSemaphoreSubmit> waits;
            for(const auto& texture : {depth.value(), field.value()->texture})
                if(auto ready = texture->get_ready_completion(); ready.is_valid())
                    waits.emplace_back(ready, Flags<PipelineStage>(PipelineStage::ComputeShader));
            submit(device, frames, waits);
            frames.wait_for_all_slots();
            const auto bytes = readback->read();
            State state;
            std::memcpy(&state, bytes.data() + sizeof(vk::DrawIndirectCommand), sizeof(state));
            if(frame == 1) {
                constexpr float dt = .1f / 6;
                for(int step = 0; step < 6; ++step) {
                    auto acceleration = data.sample(expected);
                    expected += velocity * dt + acceleration * (.5f * dt * dt);
                    velocity += acceleration * dt;
                }
                EXPECT_LT(Math::length(Math::Vec3(state.position_age) - expected), .00001);
                EXPECT_LT(Math::length(Math::Vec3(state.velocity) - velocity), .00001);
                previous = bytes;
            } else if(frame > 1)
                EXPECT_EQ(bytes, previous);
        }
    }

}
