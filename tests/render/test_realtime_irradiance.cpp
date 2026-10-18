#include "support/render_graph_gpu_fixture.h"

#include "asset/data/irradiance_data.h"
#include "graphics/gpu_timer.h"
#include "render/ray_scene.h"
#include "render/passes/realtime_irradiance_pass.h"
#include "render/resource/irradiance.h"
#include "render/material/view_lighting.h"
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
            data.probes.resize(volume.probe_count());
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

    class RealtimeIrradianceGpuTest: public RenderGraphGpuTest {
    protected:
        static IrradianceData read_probes(FrameScheduler& frames, RealtimeIrradiancePass& pass,
            const ProbeScene& scene, const RenderIrradianceVolume& settings,
            const std::shared_ptr<Readback>& readback, uint64_t lifetime = 1,
            bool measure = false) {
            begin_frame(frames);
            auto result = pass.update(frames, lifetime, scene.snapshot, std::span(&settings, 1),
                settings.update_budget, measure);
            EXPECT_TRUE(result) << result.error().message;
            if(!result)
                return {};
            copy_probes(frames, *result.value().front().resource, readback);
            if(HasFatalFailure())
                return {};
            submit(frames.get_device(), frames);
            frames.wait_for_all_slots();
            return decode_probes(*result.value().front().resource, *readback);
        }
    };

    TEST_F(RealtimeIrradianceGpuTest, PreparesGiOnlyForViewsThatActuallyRasterize) {
        auto& renderer = engine->get_renderer();
        ProbeScene source;
        source.world.render_items[0].mesh = lit_quad();
        auto baked = region();
        baked.entity_id = EntityId(9);
        baked.realtime = false;
        baked.update_budget = 0;
        source.world.irradiance_volumes.push_back({.volume = baked});
        source.world.irradiance_volumes.push_back({.volume = region()});
        unsigned mesh_reads = 0;
        renderer.set_ray_scene_sources([&](AssetHandle) {
            ++mesh_reads;
            return Result<MeshData, Error>::success(source.mesh);
        });
        ASSERT_TRUE(prepare_offscreen_host({33, 33}));
        source.world.cameras.push_back(
            {.primary = true, .view_matrix = Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0})});
        RenderView view{
            .path_trace_enabled = true, .path_trace_settings = {.samples = 1, .max_dimension = 32}};
        const auto draw = [&] {
            ASSERT_TRUE(renderer.set_render_view(view));
            const auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error().message;
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            ASSERT_TRUE(renderer.render_frame(std::make_shared<RenderWorld>(source.world)));
            renderer.wait_idle();
        };
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_TRUE(renderer.path_trace_error().empty());
        EXPECT_EQ(renderer.path_trace_samples(), 1u);
        EXPECT_EQ(renderer.get_irradiance_statistics().probes, 0u);
        EXPECT_EQ(mesh_reads, 1u);

        // 不支持的预览内容会回退光栅，仍需要正常的 GI。
        source.world.environment.fog_enabled = true;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_FALSE(renderer.path_trace_error().empty());
        EXPECT_EQ(renderer.get_irradiance_statistics().probes, 8u);
        EXPECT_EQ(mesh_reads, 2u);

        auto& selected = source.world.irradiance_volumes.back().volume;
        selected.half_extents = {1, 1, 1};
        selected.update_budget = 3;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(renderer.get_irradiance_statistics().probes, 64u);
        EXPECT_EQ(renderer.get_irradiance_statistics().updated_probes, 3u);
        EXPECT_EQ(mesh_reads, 2u);

        auto second = selected;
        second.entity_id = EntityId(10);
        second.center.x += 1;
        source.world.irradiance_volumes.push_back({.volume = second});
        source.world.environment.realtime_gi_budget = 1;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(renderer.get_irradiance_statistics().regions, 2u);
        EXPECT_EQ(renderer.get_irradiance_statistics().probes, 128u);
        EXPECT_EQ(renderer.get_irradiance_statistics().updated_probes, 1u);
        EXPECT_GE(renderer.get_irradiance_statistics().evaluated_probes, 4u);
        EXPECT_EQ(mesh_reads, 2u);

        view.visible = false;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(mesh_reads, 2u);
        renderer.set_ray_scene_sources({});
    }

    TEST_F(RealtimeIrradianceGpuTest, RealtimeIrradianceFollowsLightsWithoutRebuildingGeometry) {
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
        const auto draw = [&] {
            return read_probes(frames, pass, scene, settings, readback, 1, true);
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

    TEST_F(RealtimeIrradianceGpuTest, RealtimeIrradianceBudgetsUpdatesAndInvalidatesHistory) {
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
        const auto draw = [&] {
            return read_probes(frames, pass, scene, settings, readback, lifetime);
        };
        const auto active = [](const IrradianceData& data) {
            return std::ranges::count_if(data.probes, &IrradianceProbe::active);
        };
        const auto first = draw();
        EXPECT_EQ(active(first), 1);
        EXPECT_EQ(pass.statistics().evaluated_probes, 1u);
        const auto second = draw();
        EXPECT_EQ(active(second), 2);
        EXPECT_EQ(pass.statistics().evaluated_probes, 2u);
        EXPECT_EQ(std::memcmp(&first.probes[0], &second.probes[0], sizeof(IrradianceProbe)), 0);
        for(int i = 0; i < 6; ++i)
            EXPECT_EQ(active(draw()), i + 3);
        scene.world.render_items[0].model_matrix[0].x = -1;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        for(int i = 0; i < 8; ++i)
            EXPECT_EQ(active(draw()), 8); // 镜像缩放仍是朝上的地面，不能误判为实心内部。
        scene.world.render_items[0].model_matrix = Math::Mat4(1);
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        ++lifetime;
        EXPECT_EQ(active(draw()), 1);
        scene.world.render_items[0].model_matrix[3].x = .1f;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        EXPECT_EQ(active(draw()), 2); // 变换保留未更新探针，继续轮流接纳遮挡变化。
        ++scene.world.render_items[0].mesh_revision;
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
        EXPECT_FALSE(pass.update(
            frames, lifetime, scene.snapshot, std::span(&settings, 1), settings.update_budget));
        settings = region();
        settings.spacing = .1f;
        settings.half_extents = {100, 100, 100};
        EXPECT_FALSE(pass.update(
            frames, lifetime, scene.snapshot, std::span(&settings, 1), settings.update_budget));
        EXPECT_FALSE(pass.update(
            frames, lifetime, std::make_shared<RayScene>(), std::span(&settings, 1), 8));
    }
    TEST_F(RealtimeIrradianceGpuTest, RealtimeIrradianceVisibilityKeepsLightBehindTheWall) {
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
        for(int i = 0; i < 24; ++i)
            data = read_probes(frames, pass, scene, settings, readback);
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
        const auto closed = scene.snapshot;
        const auto upload = pass.statistics().geometry_upload_bytes;
        scene.world.render_items.back().model_matrix[3].x = 100;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        EXPECT_EQ(scene.snapshot->geometry->topology, closed->geometry->topology);
        EXPECT_EQ(scene.builder.statistics().geometry_builds, 1u);
        EXPECT_EQ(scene.builder.statistics().geometry_refits, 1u);
        for(int i = 0; i < 16; ++i)
            data = read_probes(frames, pass, scene, settings, readback);
        const auto open = data.sample({.25f, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(open);
        EXPECT_GT(open->r, .08f);
        EXPECT_GT(open->r, hidden->r * 8);
        EXPECT_GT(pass.statistics().geometry_upload_bytes, upload);
        const auto stable_upload = pass.statistics().geometry_upload_bytes;
        scene.world.render_items.back().model_matrix[3].x = 0;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        for(int i = 0; i < 16; ++i)
            data = read_probes(frames, pass, scene, settings, readback);
        const auto closed_again = data.sample({.25f, 0, 1}, {0, 0, -1});
        ASSERT_TRUE(closed_again);
        EXPECT_LT(closed_again->r, open->r * .12f);
        const auto stopped_upload = pass.statistics().geometry_upload_bytes;
        EXPECT_GT(stopped_upload, stable_upload);
        read_probes(frames, pass, scene, settings, readback);
        EXPECT_EQ(pass.statistics().geometry_upload_bytes, stopped_upload);
    }

    TEST_F(RealtimeIrradianceGpuTest, RealtimeIrradianceIsSampledByForwardAndDeferredSurfaces) {
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
                auto result = pass.update(
                    frames, 1, source.snapshot, std::span(&settings, 1), settings.update_budget);
                ASSERT_TRUE(result) << result.error().message;
                const ResolvedIrradianceVolume live{settings, result.value().front().resource};
                const RenderSubmission submission{world, camera, {}, {}, std::span(&live, 1)};
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

    TEST_F(RealtimeIrradianceGpuTest, ProbeDebugShowsValidityUpdatesLightingAndCanBeDisabled) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        IrradianceData data;
        data.origin = {-.5f, -.5f, -.5f};
        data.spacing = {1, 1, 1};
        data.dimensions = {2, 2, 2};
        data.max_distance = 100;
        data.probes.resize(8);
        data.probes[0].coefficients[0] = {1, 0, 0, 1};
        auto created = Irradiance::try_create(engine->get_render_resources(), data);
        ASSERT_TRUE(created);
        const auto resource = created.value();
        auto volume = region();
        volume.center = {};
        volume.half_extents = {1, 1, 1};
        const ResolvedIrradianceVolume live{.volume = volume,
            .resource = resource,
            .updated_first = 0,
            .updated_count = 1,
            .evaluated_probes = 2};
        auto unrelated = live;
        unrelated.resource = std::make_shared<Irradiance>();
        RenderWorld world;
        world.irradiance_volumes.push_back({.volume = volume});
        const ViewProjectMatrix camera{Math::look_at({3, 4, 5}, {0, 0, 0}, {0, 1, 0}),
            Math::ortho(-1.6f, 1.6f, -1.6f, 1.6f, .1f, 20)};
        constexpr Math::Vec2u size{129, 129};
        const auto pixel = [&](size_t index) {
            auto position =
                camera.projection * camera.view * Math::Vec4(data.probe_position(index), 1);
            return Math::Vec2u(
                Math::Vec2(position.x, -position.y) / position.w * Math::Vec2(size) * .5f
                + Math::Vec2(size) * .5f);
        };
        for(const auto& [path, realtime] :
            {std::pair{RenderPath::Forward, true}, std::pair{RenderPath::Deferred, true},
                std::pair{RenderPath::Forward, false}, std::pair{RenderPath::Deferred, false}}) {
            SCOPED_TRACE(int(path));
            SCOPED_TRACE(realtime);
            world.irradiance_volumes.front().volume.realtime = realtime;
            world.irradiance_volumes.front().resource = realtime ? nullptr : resource;
            MaterialPrograms programs;
            const auto samples =
                path == RenderPath::Forward ? SampleCount::Count4 : SampleCount::Count1;
            auto renderer =
                create_scene(programs, size, samples, path == RenderPath::Deferred, path);
            ASSERT_TRUE(renderer);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(1);
            FrameWait wait{device, frames};
            auto readback = std::make_shared<Readback>(
                device, device.get_capability().physical_device, size.x * size.y * 4);
            for(const auto mode : {IrradianceDebugMode::Off, IrradianceDebugMode::Status,
                    IrradianceDebugMode::Lighting, IrradianceDebugMode::Off}) {
                SCOPED_TRACE(int(mode));
                begin_frame(frames);
                const RenderSubmission submission{
                    world, camera, {}, {}, std::span(realtime ? &live : &unrelated, 1), mode};
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
                const auto rgb = [&](Math::Vec2u p) {
                    return read_sdr_pixel(bytes, size, format, p);
                };
                const auto first = rgb(pixel(0));
                if(mode == IrradianceDebugMode::Off) {
                    EXPECT_LT(Math::length(first), 3);
                } else if(mode == IrradianceDebugMode::Lighting) {
                    EXPECT_GT(first.r, 60);
                    EXPECT_LT(first.g, 10);
                } else {
                    const auto invalid = rgb(pixel(1));
                    const auto pending = rgb(pixel(2));
                    EXPECT_GT(first.g, first.r * 2);
                    EXPECT_GT(invalid.r, invalid.g * 2);
                    if(realtime)
                        EXPECT_NEAR(pending.r, pending.g, 2);
                    else
                        EXPECT_GT(pending.r, pending.g * 2);
                    EXPECT_GT(pending.r, 20);
                    bool highlighted = false;
                    const auto center = pixel(0);
                    for(int y = -5; y <= 5; ++y)
                        for(int x = -5; x <= 5; ++x) {
                            const auto color = rgb({center.x + x, center.y + y});
                            highlighted |= color.r > 150 && color.g > 140 && color.b < 110;
                        }
                    EXPECT_EQ(highlighted, realtime);
                }
            }
        }
    }

    TEST_F(RealtimeIrradianceGpuTest,
        RealtimeIrradianceKeepsQueuedVersionsThroughResetAndSceneChange) {
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
        auto second_readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        std::array<std::shared_ptr<Irradiance>, 2> last;
        for(unsigned frame = 0; frame < 8; ++frame) {
            scene.world.lights[0].color = frame % 2 ? Math::Vec3(0, 1, 0) : Math::Vec3(1, 0, 0);
            if(frame == 3)
                scene.world.render_items[0].model_matrix[3].x = .1f;
            if(frame == 4)
                settings.center.x = .1f;
            ASSERT_NO_FATAL_FAILURE(scene.capture());
            begin_frame(frames);
            auto second = settings;
            second.entity_id = 2;
            second.center.x += .5f;
            const std::array regions{settings, second};
            auto updated = pass.update(frames, frame < 3 ? 1 : 2, scene.snapshot, regions, 16);
            ASSERT_TRUE(updated) << updated.error().message;
            if(frame == 5)
                pass.reset(); // 已录制的旧代仍由 GPU 帧槽保活。
            if(frame == 7) {
                last = {updated.value()[0].resource, updated.value()[1].resource};
                ASSERT_NO_FATAL_FAILURE(copy_probes(frames, *last[0], readback));
                ASSERT_NO_FATAL_FAILURE(copy_probes(frames, *last[1], second_readback));
            }
            submit(device, frames);
        }
        frames.wait_for_all_slots();
        for(unsigned i = 0; i < last.size(); ++i) {
            ASSERT_TRUE(last[i]);
            const auto data = decode_probes(*last[i], i ? *second_readback : *readback);
            const auto color = data.sample(last[i]->origin, {0, 0, -1});
            ASSERT_TRUE(color);
            EXPECT_GT(color->g, .12f);
            EXPECT_LT(color->r, .001f);
        }
    }

    TEST_F(RealtimeIrradianceGpuTest, MultipleRegionsShareInputsAndPreserveIndependentHistories) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        std::array<RenderIrradianceVolume, 4> regions;
        for(unsigned i = 0; i < regions.size(); ++i) {
            regions[i] = region();
            regions[i].entity_id = i + 1;
            regions[i].center.x = float(i);
        }
        regions[0].update_budget = 1;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        std::vector<ResolvedIrradianceVolume> outputs;
        const auto draw = [&](uint32_t budget, uint64_t lifetime = 1) {
            begin_frame(frames);
            auto updated = pass.update(frames, lifetime, scene.snapshot, regions, budget);
            ASSERT_TRUE(updated) << updated.error().message;
            outputs = std::move(updated).value();
            ASSERT_EQ(outputs.size(), regions.size());
            EXPECT_EQ(pass.statistics().regions, 4u);
            EXPECT_EQ(pass.statistics().probes, 32u);
            EXPECT_EQ(pass.statistics().updated_probes, budget);
            EXPECT_EQ(pass.statistics().image_bytes, 32u * sizeof(IrradianceProbe) * 2);
            // 新区域即使没有分到预算也必须可安全采样，不能含未初始化数据。
            ASSERT_NO_FATAL_FAILURE(copy_probes(frames, *outputs.back().resource, readback));
            submit(device, frames);
            frames.wait_for_all_slots();
        };
        ASSERT_NO_FATAL_FAILURE(draw(1));
        const auto pending = decode_probes(*outputs.back().resource, *readback);
        EXPECT_EQ(std::ranges::count_if(pending.probes, &IrradianceProbe::active), 0);
        const auto uploaded = pass.statistics().upload_bytes;
        for(unsigned i = 0; i < 7; ++i)
            ASSERT_NO_FATAL_FAILURE(draw(1));
        for(const auto& output : outputs)
            EXPECT_EQ(output.evaluated_probes, 2u);
        EXPECT_EQ(pass.statistics().upload_bytes, uploaded);

        regions[0].center.x += .1f;
        ASSERT_NO_FATAL_FAILURE(draw(4));
        EXPECT_EQ(outputs[0].evaluated_probes, 1u);
        for(unsigned i = 1; i < outputs.size(); ++i)
            EXPECT_EQ(outputs[i].evaluated_probes, 3u);
        EXPECT_EQ(pass.statistics().upload_bytes, uploaded);
        std::swap(regions[0], regions[3]);
        ASSERT_NO_FATAL_FAILURE(draw(4));
        EXPECT_EQ(outputs[0].evaluated_probes, 4u);
        EXPECT_EQ(outputs[3].evaluated_probes, 2u);
        ASSERT_NO_FATAL_FAILURE(draw(4, 2));
        for(const auto& output : outputs)
            EXPECT_EQ(output.evaluated_probes, 1u);
        EXPECT_EQ(pass.statistics().upload_bytes, uploaded);
        begin_frame(frames);
        regions[1].entity_id = regions[0].entity_id;
        EXPECT_FALSE(pass.update(frames, 2, scene.snapshot, regions, 4));
    }

    TEST_F(RealtimeIrradianceGpuTest, SelectionUsesVisibleBoundsDistanceAndStableIdentity) {
        IrradianceData data;
        data.origin = {0, 0, -4};
        data.spacing = {1, 1, 1};
        data.probes.resize(8);
        auto created = Irradiance::try_create(engine->get_render_resources(), data);
        ASSERT_TRUE(created);
        RenderWorld world;
        const std::array<float, 6> positions{4, 0, 2, 0, 1, 100};
        for(unsigned i = 0; i < positions.size(); ++i) {
            auto resource = std::make_shared<Irradiance>(*created.value());
            resource->origin.x = positions[i];
            world.irradiance_volumes.push_back({{.entity_id = 6 - i,
                                                    .intensity = float(i + 1),
                                                    .normal_bias = .01f * i,
                                                    .blend_distance = .25f * i},
                resource});
        }
        const auto selected = ViewLightingData::prepare(
            {world, ViewProjectMatrix{Math::Mat4(1), Math::ortho(-10, 10, -10, 10, .1f, 100)}}, {});
        EXPECT_EQ(selected.irradiance_volume_count, 4u);
        EXPECT_EQ(selected.excess_irradiance_volumes, 1u); // 屏外区域不计入超预算。
        EXPECT_EQ(selected.irradiance_probes, 32u);
        const std::array<unsigned, 4> expected{3, 1, 4, 2};
        for(unsigned i = 0; i < expected.size(); ++i) {
            const auto& resolved = world.irradiance_volumes[expected[i]];
            EXPECT_EQ(selected.irradiance[i], resolved.resource);
            const auto& parameters = selected.lighting.irradiance_volumes[i];
            EXPECT_EQ(parameters.origin_intensity,
                Math::Vec4(resolved.resource->origin, resolved.volume.intensity));
            EXPECT_EQ(parameters.spacing_bias,
                Math::Vec4(resolved.resource->spacing, resolved.volume.normal_bias));
            EXPECT_EQ(
                parameters.dimensions_blend, Math::Vec4(Math::Vec3(resolved.resource->dimensions),
                                                 resolved.volume.blend_distance));
        }
    }

    TEST_F(RealtimeIrradianceGpuTest, OverlappingBakedAndLiveRegionsBlendWithoutAddingEnergy) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        std::array<std::shared_ptr<Irradiance>, 2> resources;
        for(unsigned i = 0; i < resources.size(); ++i) {
            IrradianceData data;
            // 接收面紧邻两个区域共有的 Y 边界，不能把 X 方向的混合压平成等权。
            data.origin = {i ? .125f : -1.375f, .9f, -1};
            data.spacing = {1.25f, 2, 2};
            data.probes.resize(8);
            for(auto& probe : data.probes) {
                probe.coefficients[0] = Math::Vec4(
                    (i ? Math::Vec3(0, .4f, 0) : Math::Vec3(.4f, 0, 0)) / .2820947918f, 1);
                probe.visibility.fill({100, 10000});
            }
            auto created = Irradiance::try_create(engine->get_render_resources(), data);
            ASSERT_TRUE(created) << created.error().message;
            resources[i] = std::move(created).value();
        }
        auto material = std::make_shared<Material>("GI blend receiver", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        const ViewProjectMatrix camera{
            Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}), Math::ortho(-1, 1, -1, 1, .1f, 10)};
        constexpr Math::Vec2u size{65, 33};
        for(const auto path : {RenderPath::Forward, RenderPath::Deferred}) {
            SCOPED_TRACE(int(path));
            MaterialPrograms programs;
            auto renderer = create_scene(programs, size, SampleCount::Count1, true, path);
            ASSERT_TRUE(renderer);
            FrameScheduler frames(device, 2);
            frames.initialize_swapchain_images(1);
            FrameWait wait{device, frames};
            RenderWorld world{
                .render_items = {{.mesh = lit_quad(), .material = {.resource = material}}}};
            world.irradiance_volumes = {{{.entity_id = 1}, resources[0]},
                {{.entity_id = 2}, resources[1]}, {{.entity_id = 3}, resources[0]},
                {{.entity_id = 4}, resources[1]}};
            ResolvedIrradianceVolume live = world.irradiance_volumes[1];
            auto readback = std::make_shared<Readback>(
                device, device.get_capability().physical_device, size.x * size.y * 4);
            for(unsigned frame = 0; frame < 8; ++frame) {
                // 静态／静态与静态／实时使用同一条采样；移除后不能残留旧描述符。
                const bool mixed = frame >= 3 && frame < 6;
                if(world.irradiance_volumes.size() > 1)
                    world.irradiance_volumes[1].volume.realtime = mixed;
                if(frame == 6)
                    world.irradiance_volumes.resize(1);
                begin_frame(frames);
                const RenderSubmission submission{world, camera, {}, {},
                    mixed ? std::span(&live, 1) : std::span<const ResolvedIrradianceVolume>{}};
                RenderGeometry geometry;
                geometry.prepare(world);
                auto drawn = renderer.value()->render(frames, submission, geometry);
                ASSERT_TRUE(drawn) << drawn.error().message;
                const auto image = renderer.value()->get_offscreen_color_view(
                    frames.get_current_frame_slot_index());
                ASSERT_NO_FATAL_FAILURE(
                    finish_readback(*renderer.value(), frames, readback, size, drawn.value()));
                const auto bytes = readback->read();
                if(frame < 2)
                    continue; // TAA 首帧初始化后再比较稳定区域。
                for(unsigned x : {5u, 19u, 32u, 45u, 59u}) {
                    const float position = (float(x) + .5f) / size.x * 2 - 1;
                    const auto smooth = [](float v) {
                        v = std::clamp(v, 0.f, 1.f);
                        return v * v * (3 - 2 * v);
                    };
                    const float left = smooth(.5f - position);
                    const float right = frame < 6 ? smooth(position + .5f) : 0;
                    const float denominator = std::max(left + right, 1e-6f);
                    const auto color =
                        read_sdr_pixel(bytes, size, image->get_image()->get_info().format, {x, 16});
                    EXPECT_NEAR(color.r, mapped_byte(.4f * .96f * left / denominator), 5);
                    EXPECT_NEAR(color.g, mapped_byte(.4f * .96f * right / denominator), 5);
                    EXPECT_LT(color.b, 3);
                }
            }
        }
    }
    TEST_F(
        RealtimeIrradianceGpuTest, MultiBounceFeedsPreviousVisibleProbesWithinTheExistingBudget) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        scene.world.lights.clear();
        ASSERT_TRUE(scene.material->set_vector_property("emissive_color", {.15f, 0, 0, 1}));
        auto wall = scene.world.render_items[0];
        wall.entity_id = EntityId(2);
        wall.model_matrix =
            Math::translate(Math::Mat4(1), {0, 0, 2}) * Math::scale(Math::Mat4(1), {-1, 1, -1});
        wall.material.resource = std::make_shared<Material>("bounce wall", "pbr");
        ASSERT_TRUE(std::const_pointer_cast<Material>(wall.material.resource)
                ->set_vector_property("base_color", {.8f, .8f, .8f, 1}));
        scene.world.render_items.push_back(wall);
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        auto settings = region();
        settings.half_extents = {4, 4, 1};
        settings.spacing = 4;
        settings.history_weight = .8f;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        const auto converge = [&] {
            double energy = 0;
            for(int frame = 0; frame < 64; ++frame) {
                auto data = read_probes(frames, pass, scene, settings, readback);
                EXPECT_TRUE(data.validate());
                if(frame >= 56)
                    for(const auto& probe : data.probes)
                        energy += probe.coefficients[0].r;
            }
            return energy;
        };
        const auto one_hop = converge();
        const auto uploaded = pass.statistics().geometry_upload_bytes;
        settings.bounce_strength = .8f;
        const auto multiple = converge();
        EXPECT_GT(one_hop, .01);
        EXPECT_GT(multiple, one_hop * 1.02);
        EXPECT_LT(multiple, one_hop * 10);
        EXPECT_EQ(pass.statistics().geometry_upload_bytes, uploaded);
        EXPECT_EQ(pass.statistics().updated_probes, 8u);
        settings.bounce_strength = 2;
        EXPECT_FALSE(settings.realtime_dimensions());
    }

    TEST_F(
        RealtimeIrradianceGpuTest, CrossRegionFeedbackReadsPreviousHistoriesAndNormalizesOverlap) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(1);
        FrameWait wait{device, frames};
        RealtimeIrradiancePass pass(device);
        ProbeScene scene;
        scene.world.lights.clear();
        ASSERT_TRUE(scene.material->set_vector_property("emissive_color", {.15f, 0, 0, 1}));
        auto wall = scene.world.render_items.front();
        wall.entity_id = EntityId(2);
        wall.model_matrix =
            Math::translate(Math::Mat4(1), {0, 0, 2}) * Math::scale(Math::Mat4(1), {-1, 1, -1});
        auto grey = std::make_shared<Material>("cross region wall", "pbr");
        ASSERT_TRUE(grey->set_vector_property("base_color", {.8f, .8f, .8f, 1}));
        wall.material.resource = grey;
        scene.world.render_items.push_back(wall);
        ASSERT_NO_FATAL_FAILURE(scene.capture());
        auto first = region();
        first.center.z = .5f;
        first.half_extents = {4, 4, .25f};
        first.spacing = 4;
        first.history_weight = .5f;
        first.bounce_strength = .75f;
        auto second = first;
        second.entity_id = EntityId(2);
        second.center.z = 1.75f;
        second.half_extents.z = .5f;
        auto readback = std::make_shared<Readback>(
            device, device.get_capability().physical_device, 8 * sizeof(IrradianceProbe));
        const auto converge = [&](std::vector<RenderIrradianceVolume> regions) {
            pass.reset();
            double energy = 0;
            for(int frame = 0; frame < 64; ++frame) {
                begin_frame(frames);
                auto updated =
                    pass.update(frames, 1, scene.snapshot, regions, uint32_t(regions.size()) * 8);
                EXPECT_TRUE(updated) << updated.error().message;
                if(!updated)
                    return 0.;
                EXPECT_EQ(pass.statistics().updated_probes, 8 * regions.size());
                EXPECT_EQ(pass.statistics().rays, 8 * regions.size() * (128 + 64));
                const auto found = std::ranges::find(updated.value(), first.entity_id,
                    [](const auto& value) { return value.volume.entity_id; });
                if(found == updated.value().end()) {
                    ADD_FAILURE() << "Missing first region";
                    return 0.;
                }
                copy_probes(frames, *found->resource, readback);
                submit(device, frames);
                frames.wait_for_all_slots();
                auto data = decode_probes(*found->resource, *readback);
                EXPECT_TRUE(data.validate());
                if(frame >= 56)
                    for(const auto& probe : data.probes)
                        energy += probe.coefficients[0].r;
            }
            return energy;
        };
        const auto alone = converge({first});
        const auto connected = converge({first, second});
        const auto reordered = converge({second, first});
        auto duplicate = first;
        duplicate.entity_id = EntityId(3);
        const auto overlapping = converge({first, duplicate});
        EXPECT_GT(alone, .01);
        EXPECT_GT(connected, alone * 1.01);
        EXPECT_NEAR(connected, reordered, connected * .0001);
        EXPECT_NEAR(overlapping, alone, alone * .0001);
    }

}
