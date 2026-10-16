#include "support/render_graph_gpu_fixture.h"

#include "asset/data/irradiance_data.h"
#include "graphics/gpu_timer.h"
#include "render/ray_scene.h"
#include "render/passes/realtime_irradiance_pass.h"
#include "render/resource/irradiance.h"
#include "render/scene/render_submission.h"

namespace Comet::Tests {
    namespace {
        struct ProbeScene {
            RaySceneBuilder builder;
            RenderWorld world;
            MeshData mesh;
            std::shared_ptr<const RayScene> snapshot;
            std::shared_ptr<Material> material;
            ProbeScene() {
                material = std::make_shared<Material>("diffuse floor", "pbr");
                EXPECT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
                EXPECT_TRUE(material->set_scalar_property("roughness", 1));
                world.render_items.push_back(
                    {.mesh_handle = AssetHandle(1), .material = {.resource = material}});
                world.lights.push_back({.direction = {0, 0, -1}, .color = {1, 0, 0}});
                mesh.vertices = {{{-20, -20, 0}, {}, {0, 0, 1}}, {{20, -20, 0}, {}, {0, 0, 1}},
                    {{0, 20, 0}, {}, {0, 0, 1}}};
                mesh.indices = {0, 1, 2};
            }
            void capture() {
                auto result = builder.update(
                    world, [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); });
                ASSERT_TRUE(result) << result.error();
                snapshot = std::move(result).value();
            }
        };

        // 读回后恢复生产链的已发布布局，下一帧仍可交替使用两张历史。
        void copy_probes(FrameScheduler& frames, const Irradiance& volume,
            const std::shared_ptr<RenderGpuTest::Readback>& destination) {
            const auto stages =
                Flags<PipelineStage>(PipelineStage::ComputeShader) | PipelineStage::FragmentShader;
            RenderGraph graph;
            const auto source = graph.import_image(
                "probe atlas", *resolve_image_state(ResourceUsage::SampledRead,
                                   {.aspects = Flags<ImageAspect>(ImageAspect::Color)}, stages));
            const auto target =
                graph.import_buffer("probe readback", {{}, 0, destination->get_size()});
            graph.add_pass({"read probes", {{source, ResourceUsage::TransferSource, {}},
                                               {target, ResourceUsage::TransferDestination, {}}}});
            graph.export_resource({source, ResourceUsage::SampledRead, stages});
            graph.export_resource({target, ResourceUsage::HostRead, {}});
            auto plan = graph.compile();
            ASSERT_TRUE(plan) << plan.error();
            const auto image = volume.probes->get_image_view()->get_image();
            const std::array<RenderGraph::Binding, 2> bindings{
                image, std::static_pointer_cast<Buffer>(destination)};
            ASSERT_TRUE(plan.value().record(frames, bindings, [&](auto, CommandBuffer& command) {
                vk::BufferImageCopy region;
                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                const auto extent = image->get_info().extent;
                region.imageExtent = vk::Extent3D(extent.x, extent.y, 1);
                command.get().copyImageToBuffer(
                    image->get(), vk::ImageLayout::eTransferSrcOptimal, destination->get(), region);
                return Result<void, GraphicsError>::success();
            }));
        }

        IrradianceData decode_probes(
            const Irradiance& volume, const RenderGpuTest::Readback& readback) {
            IrradianceData data;
            data.origin = volume.origin;
            data.spacing = volume.spacing;
            data.dimensions = volume.dimensions;
            data.max_distance = 100;
            data.probes.resize(volume.dimensions.x * volume.dimensions.y * volume.dimensions.z);
            const auto bytes = readback.read();
            std::memcpy(data.probes.data(), bytes.data(), bytes.size());
            return data;
        }

        RenderIrradianceVolume region() {
            return {.entity_id = EntityId(1),
                .realtime = true,
                .center = {0, 0, 1},
                .half_extents = {.5f, .5f, .5f},
                .spacing = .5f,
                .update_budget = 8,
                .update_rays = 128,
                .history_weight = .5f};
        }
    }

    TEST_F(RenderGraphGpuTest, RealtimeIrradianceFollowsLightsWithoutRebuildingGeometry) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        auto settings = region();
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        const auto draw = [&]() -> IrradianceData {
            begin_frame(frames);
            auto result = pass.update(frames, 1, scene.snapshot, settings, true);
            EXPECT_TRUE(result) << result.error().message;
            if(!result)
                return {};
            copy_probes(frames, *result.value(), readback);
            submit(device, frames);
            frames.wait_for_all_slots();
            return decode_probes(*result.value(), *readback);
        };
        IrradianceData red;
        for(int i = 0; i < 8; ++i)
            red = draw();
        ASSERT_EQ(red.probes.size(), 8u);
        const auto initial = red.sample({0, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(initial);
        EXPECT_GT(initial->r, .12f);
        EXPECT_LT(initial->g, .001f);
        EXPECT_TRUE(red.validate()) << red.validate().error();
        const auto uploaded = pass.statistics().geometry_upload_bytes;
        const auto geometry = scene.snapshot->geometry;
        scene.world.lights[0].color = {0, 1, 0};
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        EXPECT_EQ(scene.snapshot->geometry, geometry);
        const auto transition = draw().sample({0, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(transition);
        EXPECT_GT(transition->r, .025f);
        EXPECT_GT(transition->g, .025f);
        IrradianceData green;
        for(int i = 0; i < 10; ++i)
            green = draw();
        const auto changed = green.sample({0, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(changed);
        EXPECT_GT(changed->g, .12f);
        EXPECT_LT(changed->r, .002f);
        EXPECT_EQ(pass.statistics().geometry_upload_bytes, uploaded);
        EXPECT_EQ(pass.statistics().updated_probes, 8u);
        EXPECT_EQ(pass.statistics().rays, 8u * (128 + 64));
        EXPECT_EQ(pass.statistics().image_bytes, 8u * sizeof(IrradianceProbe) * 2);
        if(GpuTimer::elapsed_ms(
               0, 0, GpuTimer::capabilities(device, frames.get_queue_family_index()))) {
            ASSERT_TRUE(pass.statistics().gpu);
            EXPECT_GT(pass.statistics().gpu->serial, 0u);
            EXPECT_GE(pass.statistics().gpu->milliseconds, 0);
        }
        // 空间中的不同位置从相邻探针采样，无需静态 lightmap UV。
        EXPECT_TRUE(green.sample({.3f, .2f, .9f}, {0, 0, -1}));
    }

    TEST_F(RenderGraphGpuTest, RealtimeIrradianceBudgetsUpdatesAndInvalidatesHistory) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        auto settings = region();
        settings.update_budget = 1;
        settings.history_weight = .9f;
        uint64_t lifetime = 1;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        const auto draw = [&]() -> IrradianceData {
            begin_frame(frames);
            auto result = pass.update(frames, lifetime, scene.snapshot, settings);
            EXPECT_TRUE(result) << result.error().message;
            if(!result)
                return {};
            copy_probes(frames, *result.value(), readback);
            submit(device, frames);
            frames.wait_for_all_slots();
            return decode_probes(*result.value(), *readback);
        };
        const auto active = [](const IrradianceData& data) {
            return std::ranges::count_if(data.probes, &IrradianceProbe::active);
        };
        const auto first = draw();
        EXPECT_EQ(active(first), 1);
        const auto second = draw();
        EXPECT_EQ(active(second), 2);
        EXPECT_EQ(std::memcmp(&first.probes[0], &second.probes[0], sizeof(IrradianceProbe)), 0);
        for(int i = 0; i < 6; ++i)
            EXPECT_EQ(active(draw()), i + 3);
        ++lifetime;
        EXPECT_EQ(active(draw()), 1);
        scene.world.render_items[0].model_matrix[3].x = .1f;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        EXPECT_EQ(active(draw()), 1);
        settings.center.x = .1f;
        EXPECT_EQ(active(draw()), 1);
        ASSERT_TRUE(scene.material->set_vector_property("base_color", {.25f, .25f, .25f, 1}));
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        EXPECT_EQ(active(draw()), 1);
        auto environment = std::make_shared<RayScene::Environment>();
        environment->size = {1, 1};
        environment->texels = {{.1f, .1f, .1f, 1}};
        auto changed_environment = std::make_shared<RayScene>(*scene.snapshot);
        changed_environment->environment_map = std::move(environment);
        changed_environment->environment = {-1, 1, 0, 1};
        scene.snapshot = std::move(changed_environment);
        EXPECT_EQ(active(draw()), 1);
        settings = region();
        settings.center.z = -1;
        EXPECT_EQ(active(draw()), 0); // 地面背后的探针不能向上泄漏光照。
        begin_frame(frames);
        settings.update_rays = 0;
        EXPECT_FALSE(pass.update(frames, lifetime, scene.snapshot, settings));
        settings = region();
        settings.spacing = .1f;
        settings.half_extents = {100, 100, 100};
        EXPECT_FALSE(pass.update(frames, lifetime, scene.snapshot, settings));
    }
    TEST_F(RenderGraphGpuTest, RealtimeIrradianceVisibilityKeepsLightBehindTheWall) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        scene.world.lights = {{.type = LightType::Point,
            .position = {-2, 0, 2},
            .color = {1, 0, 0},
            .intensity = 10,
            .range = 8}};
        auto wall = scene.world.render_items.front();
        auto black = std::make_shared<Material>("light barrier", "pbr");
        ASSERT_TRUE(black->set_vector_property("base_color", {0, 0, 0, 1}));
        ASSERT_TRUE(black->set_surface({.double_sided = true}));
        wall.material.resource = black;
        wall.model_matrix = Math::rotate(Math::Mat4(1), Math::radians(90.f), {0, 1, 0});
        scene.world.render_items.push_back(wall);
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        auto settings = region();
        settings.half_extents = {2, 1, .5f};
        settings.spacing = 1;
        settings.update_budget = 16;
        settings.update_rays = 256;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 16 * sizeof(IrradianceProbe));
        IrradianceData data;
        for(int i = 0; i < 24; ++i) {
            begin_frame(frames);
            auto result = pass.update(frames, 1, scene.snapshot, settings);
            ASSERT_TRUE(result) << result.error().message;
            ASSERT_NO_FATAL_FAILURE(copy_probes(frames, *result.value(), readback));
            submit(device, frames);
            frames.wait_for_all_slots();
            data = decode_probes(*result.value(), *readback);
        }
        const auto bright = data.sample({-.8f, 0, 1}, {0, 0, -1});
        const auto hidden = data.sample({.25f, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(bright);
        ASSERT_TRUE(hidden);
        EXPECT_GT(bright->r, .1f);
        EXPECT_LT(hidden->r, bright->r * .08f);
        EXPECT_TRUE(data.validate()) << data.validate().error();
        for(const auto& probe : data.probes) {
            EXPECT_TRUE(probe.active());
            for(const auto value : probe.coefficients)
                EXPECT_TRUE(Math::is_finite(value));
        }
    }

    TEST_F(RenderGraphGpuTest, RealtimeIrradianceIsSampledByForwardAndDeferredSurfaces) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        ProbeScene source;
        source.world.render_items[0].model_matrix =
            Math::translate(Math::Mat4(1), {0, 0, 2})
            * Math::rotate(Math::Mat4(1), Math::radians(180.f), {1, 0, 0});
        source.world.lights[0].direction = {0, 0, 1};
        auto settings = region();
        settings.history_weight = 0;
        auto material = std::make_shared<Material>("moving receiver", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        RenderWorld world{
            .render_items = {{.mesh = lit_quad(), .material = {.resource = material}}}};
        world.irradiance_volumes.push_back({.volume = settings});
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        constexpr Math::Vec2u size{33, 33};
        for(const auto path : {RenderPath::Forward, RenderPath::Deferred}) {
            SCOPED_TRACE(int(path));
            MaterialPrograms programs;
            auto renderer = create_scene(programs, size, SampleCount::Count1, false, path);
            ASSERT_TRUE(renderer);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(1);
            FrameWait wait{device, frames};
            RealtimeIrradiancePass pass(device);
            auto readback = std::make_shared<Readback>(
                device, device.get_capability().physical_device, size.x * size.y * 4);
            for(unsigned frame = 0; frame < 4; ++frame) {
                source.world.lights[0].color =
                    frame < 2 ? Math::Vec3(1, 0, 0) : Math::Vec3(0, 1, 0);
                ASSERT_NO_FATAL_FAILURE(source.capture());
                world.render_items[0].model_matrix =
                    Math::translate(Math::Mat4(1), {frame % 2 ? .1f : 0, 0, frame % 2 ? .2f : 0});
                begin_frame(frames);
                auto result = pass.update(frames, 1, source.snapshot, settings);
                ASSERT_TRUE(result) << result.error().message;
                const ResolvedIrradianceVolume live{settings, result.value()};
                const RenderSubmission submission{world, camera, {}, {}, &live};
                RenderGeometry geometry;
                geometry.prepare(world);
                const auto drawn = renderer.value()->render(frames, submission, geometry);
                ASSERT_TRUE(drawn) << drawn.error().message;
                const auto output = renderer.value()->get_offscreen_color_view(
                    frames.get_current_frame_slot_index());
                ASSERT_NO_FATAL_FAILURE(
                    finish_readback(*renderer.value(), frames, readback, size, drawn.value()));
                const auto bytes = readback->read();
                const auto format = output->get_image()->get_info().format;
                const bool bgra =
                    format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
                const auto index = (16 * size.x + 16) * 4;
                const auto red = std::to_integer<int>(bytes[index + (bgra ? 2 : 0)]);
                const auto green = std::to_integer<int>(bytes[index + 1]);
                if(frame < 2) {
                    EXPECT_GT(red, 60);
                    EXPECT_LT(green, 3);
                } else {
                    EXPECT_GT(green, 60);
                    EXPECT_LT(red, 3);
                }
                EXPECT_EQ(renderer.value()->get_material_statistics().irradiance_probes, 8u);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, RealtimeIrradianceKeepsQueuedVersionsThroughResetAndSceneChange) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        auto settings = region();
        settings.history_weight = 0;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        std::shared_ptr<Irradiance> last;
        for(unsigned frame = 0; frame < 8; ++frame) {
            scene.world.lights[0].color = frame % 2 ? Math::Vec3(0, 1, 0) : Math::Vec3(1, 0, 0);
            if(frame == 3)
                scene.world.render_items[0].model_matrix[3].x = .1f;
            if(frame == 4)
                settings.center.x = .1f;
            ASSERT_NO_FATAL_FAILURE(scene.capture());
            begin_frame(frames);
            auto updated = pass.update(frames, frame < 3 ? 1 : 2, scene.snapshot, settings);
            ASSERT_TRUE(updated) << updated.error().message;
            if(frame == 5)
                pass.reset(); // 已录制的旧代仍由 GPU 帧槽保活。
            if(frame == 7) {
                last = updated.value();
                ASSERT_NO_FATAL_FAILURE(copy_probes(frames, *last, readback));
            }
            submit(device, frames);
        }
        frames.wait_for_all_slots();
        ASSERT_TRUE(last);
        const auto data = decode_probes(*last, *readback);
        const auto color = data.sample(settings.center, {0, 0, -1});
        ASSERT_TRUE(color);
        EXPECT_GT(color->g, .12f);
        EXPECT_LT(color->r, .001f);
    }

}
