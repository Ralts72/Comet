#include "support/render_gpu_test.h"
#include "render/path_trace_scene.h"
#include "render/material/material.h"
#include "asset/asset_manager.h"
#include "scene/scene_serializer.h"
#include "scene/scene.h"
#include "scene/component_registry.h"

namespace Comet::Tests {
    class PathTraceGpuTest: public RenderGpuTest {
    protected:
        static constexpr Math::Vec2u SIZE{32, 32};
        RenderScene scene;
        RenderView view;
        PathTraceSceneBuilder builder;
        RenderWorld source;
        MeshData mesh;
        std::shared_ptr<Material> material;
        std::unique_ptr<FrameScheduler> copy_frames;

        void SetUp() override {
            RenderGpuTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            ASSERT_TRUE(prepare_offscreen_host(SIZE));
            scene.cameras.push_back({.primary = true,
                .view_matrix = Math::translate(Math::Mat4(1), {0, 0, -3}),
                .projection = RenderCamera::Projection::Orthographic,
                .orthographic_height = 2});
            view.render_size = SIZE;
            view.path_trace_settings.max_dimension = 32;
            view.path_trace_settings.samples = 128;
            view.path_trace_settings.bounces = 2;
            material = std::make_shared<Material>("surface", "pbr");
            source.render_items.push_back(
                {.mesh_handle = AssetHandle(1), .material = {.resource = material}});
            mesh.vertices = {{{-20, -20, 0}, {}, {0, 0, 1}}, {{20, -20, 0}, {}, {0, 0, 1}},
                {{0, 20, 0}, {}, {0, 0, 1}}};
            mesh.indices = {0, 1, 2};
            copy_frames = std::make_unique<FrameScheduler>(
                engine->get_renderer().get_render_context().get_device(), 1);
            copy_frames->initialize_swapchain_images(1);
        }
        void TearDown() override {
            copy_frames.reset();
            view.path_trace.reset();
            builder.reset();
            RenderGpuTest::TearDown();
        }
        void capture_source() {
            builder.reset();
            auto captured = builder.update(
                source, [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); });
            ASSERT_TRUE(captured) << captured.error();
            view.path_trace = std::move(captured).value();
        }
        void draw(uint32_t frames = 1) {
            auto& renderer = engine->get_renderer();
            ASSERT_TRUE(renderer.set_render_view(view));
            for(uint32_t i = 0; i < frames; ++i) {
                const auto prepared = render_frontend->prepare_frame();
                ASSERT_TRUE(prepared) << prepared.error().message;
                ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
                const auto rendered = render_frontend->render(scene);
                ASSERT_TRUE(rendered) << rendered.error().message;
            }
        }
        std::vector<Math::Vec4> pixels() {
            auto& renderer = engine->get_renderer();
            const auto& target = renderer.get_scene_renderer();
            auto image = target.get_hdr_color_view(0)->get_image();
            const auto extent = image->get_info().extent;
            auto& device = renderer.get_render_context().get_device();
            auto destination = std::make_shared<Readback>(device,
                device.get_capability().physical_device, extent.x * extent.y * sizeof(Math::Vec4));
            begin_frame(*copy_frames);
            RenderGraph graph;
            auto initial = *resolve_image_state(ResourceUsage::SampledRead,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                Flags<PipelineStage>(PipelineStage::FragmentShader));
            initial.resource.stages |= PipelineStage::ComputeShader;
            initial.resource.access |= Access::ShaderWrite;
            const auto from = graph.import_image("trace", initial);
            const auto to = graph.import_buffer("readback", {{}, 0, destination->get_size()});
            graph.add_pass({"copy", {{from, ResourceUsage::TransferSource, {}},
                                        {to, ResourceUsage::TransferDestination, {}}}});
            graph.export_resource({from, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
            graph.export_resource({to, ResourceUsage::HostRead, {}});
            auto plan = graph.compile();
            if(!plan) {
                ADD_FAILURE() << plan.error();
                return {};
            }
            const std::array<RenderGraph::Binding, 2> bindings{
                image, std::static_pointer_cast<Buffer>(destination)};
            const auto recorded =
                plan.value().record(*copy_frames, bindings, [&](auto, CommandBuffer& command) {
                    vk::BufferImageCopy region;
                    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                    region.imageExtent = vk::Extent3D(extent.x, extent.y, 1);
                    command.get().copyImageToBuffer(image->get(),
                        vk::ImageLayout::eTransferSrcOptimal, destination->get(), region);
                    return Result<void, GraphicsError>::success();
                });
            EXPECT_TRUE(recorded) << recorded.error().message;
            submit(device, *copy_frames);
            copy_frames->wait_for_all_slots();
            const auto bytes = destination->read();
            std::vector<Math::Vec4> result(extent.x * extent.y);
            std::memcpy(result.data(), bytes.data(), bytes.size());
            return result;
        }
        uint32_t samples() const {
            return engine->get_renderer().get_scene_renderer().get_path_trace_statistics().samples;
        }
    };

    TEST_F(PathTraceGpuTest, AccumulationStopsAtTargetAndResetsOnlyWhenNeeded) {
        source.render_items.clear();
        source.environment.background_color = {.25f, .5f, .75f};
        capture_source();
        view.path_trace_settings.samples = 3;
        draw(5);
        EXPECT_EQ(samples(), 3u);
        auto first = pixels();
        ASSERT_EQ(first.size(), SIZE.x * SIZE.y);
        for(const auto pixel : first)
            EXPECT_EQ(pixel, Math::Vec4(.25f, .5f, .75f, 1));
        scene.post_process.exposure = 2;
        view.path_trace_settings.samples = 5;
        draw();
        EXPECT_EQ(samples(), 4u);
        EXPECT_EQ(pixels(), first);
        scene.cameras[0].view_matrix[3].x = .25f;
        draw();
        EXPECT_EQ(samples(), 1u);
        view.path_trace_settings.seed = 7;
        draw();
        EXPECT_EQ(samples(), 1u);
        source.environment.background_color = {.5f, .2f, .1f};
        capture_source();
        draw();
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(pixels()[0], Math::Vec4(.5f, .2f, .1f, 1));
        view.render_size = {64, 32};
        ASSERT_TRUE(engine->get_renderer().enable_offscreen_rendering(view.render_size));
        draw();
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(engine->get_renderer().get_scene_renderer().get_path_trace_statistics().size,
            Math::Vec2u(32, 16));
        const auto snapshot = view.path_trace;
        view.path_trace.reset();
        draw();
        EXPECT_EQ(samples(), 0u);
        view.path_trace = snapshot;
        draw();
        EXPECT_EQ(samples(), 1u);
    }

    TEST_F(PathTraceGpuTest, UnlitEmissionAndImageOrientationMatchTheCamera) {
        material = std::make_shared<Material>("emitter", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {1, .2f, 0, 1}));
        ASSERT_TRUE(material->set_scalar_property("intensity", 2));
        source.render_items[0].material.resource = material;
        mesh.vertices = {{{-.8f, .1f, 0}, {}, {0, 0, 1}}, {{.8f, .1f, 0}, {}, {0, 0, 1}},
            {{0, .95f, 0}, {}, {0, 0, 1}}};
        capture_source();
        draw(4);
        const auto image = pixels();
        ASSERT_EQ(image.size(), SIZE.x * SIZE.y);
        EXPECT_EQ(image[8 * SIZE.x + 16], Math::Vec4(2, .4f, 0, 1));
        EXPECT_EQ(image[24 * SIZE.x + 16], Math::Vec4(0, 0, 0, 1));
    }

    TEST_F(PathTraceGpuTest, AdditionalBouncesCarryIndirectLightAndSamplesConverge) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .2f, .1f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        source.environment.background_color = {1, 1, 1};
        capture_source();
        view.path_trace_settings.bounces = 1;
        draw();
        EXPECT_EQ(pixels()[SIZE.x * 16 + 16], Math::Vec4(0, 0, 0, 1));
        view.path_trace_settings.bounces = 2;
        draw();
        const auto one = pixels();
        draw(15);
        const auto sixteen = pixels();
        draw(112);
        const auto reference = pixels();
        ASSERT_EQ(samples(), 128u);
        double error_one = 0, error_sixteen = 0;
        Math::Vec3 mean{};
        for(size_t i = 0; i < reference.size(); ++i) {
            const auto a = Math::Vec3(one[i] - reference[i]);
            const auto b = Math::Vec3(sixteen[i] - reference[i]);
            error_one += glm::dot(a, a);
            error_sixteen += glm::dot(b, b);
            mean += Math::Vec3(reference[i]) / float(reference.size());
            EXPECT_TRUE(Math::is_finite(reference[i]));
        }
        EXPECT_LT(error_sixteen, error_one * .2);
        EXPECT_GT(mean.r, .45f);
        EXPECT_LT(mean.r, .7f);
        EXPECT_GT(mean.r, mean.g * 2);
        EXPECT_GT(mean.g, mean.b * 1.5f);
    }

    TEST_F(PathTraceGpuTest, AnalyticLightsUseBvhVisibility) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.8f, .8f, .8f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        ASSERT_TRUE(material->set_surface({.double_sided = true}));
        for(auto type : {LightType::Directional, LightType::Point, LightType::Spot}) {
            source.lights = {{.type = type,
                .position = {2, 0, 2},
                .direction = glm::normalize(Math::Vec3(-1, 0, -1)),
                .intensity = 10,
                .range = 20}};
            source.render_items.resize(1);
            capture_source();
            view.path_trace_settings.bounces = 1;
            draw();
            auto clear = pixels();
            ASSERT_EQ(clear.size(), SIZE.x * SIZE.y);
            EXPECT_GT(clear[16 * SIZE.x + 16].r, .15f);
            auto blocker = source.render_items[0];
            blocker.model_matrix = Math::translate(Math::Mat4(1), {1, 0, 1})
                                   * Math::scale(Math::Mat4(1), {.02f, .05f, 1});
            source.render_items.push_back(blocker);
            capture_source();
            draw();
            EXPECT_LT(pixels()[16 * SIZE.x + 16].r, .001f);
        }
    }

    TEST_F(PathTraceGpuTest, EmissiveSurfaceSamplingDoesNotDoubleCountBsdfPaths) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .2f, .1f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        auto emitter = std::make_shared<Material>("emitter", "unlit_color");
        ASSERT_TRUE(emitter->set_vector_property("color", {1, 1, 1, 1}));
        ASSERT_TRUE(emitter->set_scalar_property("intensity", 1));
        ASSERT_TRUE(emitter->set_surface({.double_sided = true}));
        auto area = source.render_items[0];
        area.material.resource = emitter;
        area.model_matrix = Math::translate(Math::Mat4(1), {0, 0, 2});
        source.render_items.push_back(area);
        scene.cameras[0].view_matrix = Math::translate(Math::Mat4(1), {0, 0, -1});
        capture_source();
        ASSERT_EQ(view.path_trace->emitters.size(), 1u);
        draw(64);
        const auto result = pixels();
        Math::Vec3 mean{};
        for(const auto pixel : result)
            mean += Math::Vec3(pixel) / float(result.size());
        EXPECT_GT(mean.r, .4f);
        EXPECT_LT(mean.r, .7f);
        EXPECT_GT(mean.r, mean.g * 2);
    }

    TEST_F(PathTraceGpuTest, MetalReflectsConstantEnvironmentWithBoundedEnergy) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.9f, .6f, .1f, 1}));
        ASSERT_TRUE(material->set_scalar_property("metallic", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", .4f));
        source.environment.background_color = {1, 1, 1};
        capture_source();
        draw(64);
        const auto result = pixels();
        Math::Vec3 mean{};
        for(const auto pixel : result) {
            EXPECT_TRUE(Math::is_finite(pixel));
            mean += Math::Vec3(pixel) / float(result.size());
        }
        EXPECT_GT(mean.r, .75f);
        EXPECT_LT(mean.r, 1.f);
        EXPECT_GT(mean.g, .5f);
        EXPECT_LT(mean.g, .68f);
        EXPECT_GT(mean.b, .08f);
        EXPECT_LT(mean.b, .15f);
    }

    TEST_F(PathTraceGpuTest, ReferenceRoomUsesPublishedMeshProductsAndMaterialSnapshots) {
        const ProjectPaths paths(COMET_SAMPLE_PROJECT_DIRECTORY);
        AssetManager assets(paths, engine->get_asset_registry(),
            engine->get_render_resource_factory(), engine->get_task_scheduler());
        static_cast<void>(assets.scan());
        auto components = create_scene_component_registry();
        auto loaded = SceneSerializer(components)
                          .load((paths.assets() / "scenes/path_tracing.scene").string());
        ASSERT_TRUE(loaded) << loaded.error();
        scene = SceneExtractor::extract(*loaded.value());
        for(const auto& item : scene.render_items) {
            const auto geometry = assets.ensure_loaded(item.mesh_handle, AssetType::Mesh);
            ASSERT_TRUE(geometry) << geometry.error().message;
            const auto surface = assets.ensure_loaded(item.material_handle, AssetType::Material);
            ASSERT_TRUE(surface) << surface.error().message;
        }
        SceneResolver resolver(engine->get_asset_registry());
        const auto world = resolver.update(scene);
        ASSERT_EQ(world->render_items.size(), scene.render_items.size());
        builder.reset();
        const auto captured = builder.update(
            *world, [&](AssetHandle handle) { return assets.read_mesh_data(handle); },
            [&](AssetHandle handle) { return assets.mesh_data_revision(handle); });
        ASSERT_TRUE(captured) << captured.error();
        view.path_trace = captured.value();
        ASSERT_GT(view.path_trace->triangles.size(), 50u);
        ASSERT_GT(view.path_trace->emitters.size(), 0u);
        view.path_trace_settings.bounces = 4;
        draw(64);
        const auto result = pixels();
        float maximum = 0;
        double energy = 0;
        for(const auto pixel : result) {
            EXPECT_TRUE(Math::is_finite(pixel));
            maximum = std::max(maximum, pixel.r);
            energy += pixel.r + pixel.g + pixel.b;
        }
        EXPECT_GT(maximum, 1.f);
        EXPECT_GT(energy / result.size(), .05);
        engine->get_renderer().wait_idle();
    }
}
