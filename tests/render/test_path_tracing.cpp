#include "support/render_gpu_test.h"
#include "support/temporary_directory.h"
#include "render/ray_scene.h"
#include "render/material/material.h"
#include "asset/asset_manager.h"
#include "scene/scene_serializer.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "render/resource/resource_factory.h"
#include "asset/data/texture_sampling.h"

#include <map>

namespace Comet::Tests {
    class PathTraceGpuTest: public RenderGpuTest {
    protected:
        static constexpr Math::Vec2u SIZE{32, 32};
        RenderScene scene;
        RenderView view;
        RaySceneBuilder builder;
        RenderWorld source;
        MeshData mesh;
        std::map<AssetHandle, TextureData> images;
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
            source.clear();
            material.reset();
            RenderGpuTest::TearDown();
        }
        void capture_source(bool reset = true) {
            if(reset)
                builder.reset();
            auto captured = builder.update(source,
                [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); }, {},
                {.texture =
                        [&](AssetHandle handle) {
                            return Result<TextureData, Error>::success(images.at(handle));
                        },
                    .environment =
                        [&](AssetHandle handle) {
                            return Result<TextureData, Error>::success(images.at(handle));
                        }});
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
        void set_texture(const std::string& property, AssetHandle handle, TextureData data) {
            const auto texture = engine->get_render_resource_factory().try_create_texture(data);
            ASSERT_TRUE(texture);
            images[handle] = std::move(data);
            material->set_texture_property(property, texture.value(), handle);
        }
        static TextureData constant_cube(const std::array<Math::Vec3, 6>& colors) {
            constexpr int side = 4;
            TextureData data{.width = side,
                .height = side,
                .format = Format::R32G32B32A32_SFLOAT,
                .pixels = std::vector<uint8_t>(6 * side * side * sizeof(Math::Vec4)),
                .cubemap = true};
            for(size_t i = 0; i < colors.size(); ++i) {
                const Math::Vec4 pixel(colors[i], 1);
                for(int texel = 0; texel < side * side; ++texel)
                    std::memcpy(data.pixels.data() + (i * side * side + texel) * sizeof(pixel),
                        &pixel, sizeof(pixel));
            }
            return data;
        }
        static Math::Vec3 mean(const std::vector<Math::Vec4>& image) {
            Math::Vec3 total(0);
            for(const auto pixel : image)
                total += Math::Vec3(pixel);
            return total / float(image.size());
        }
        void capture_project_scene(const char* filename) {
            TemporaryDirectory project;
            const ProjectPaths paths(project.path());
            std::filesystem::copy(ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).assets(),
                paths.assets(), std::filesystem::copy_options::recursive);
            ASSERT_FALSE(std::filesystem::exists(paths.cache()));
            AssetManager assets(paths, engine->get_asset_registry(),
                engine->get_render_resource_factory(), engine->get_task_scheduler());
            ASSERT_TRUE(assets.scan().succeeded());
            auto components = create_scene_component_registry();
            auto loaded = SceneSerializer(components, assets.scene_sources(components))
                              .load((paths.assets() / filename).string());
            ASSERT_TRUE(loaded) << loaded.error();
            scene = SceneExtractor::extract(*loaded.value());
            for(const auto& item : scene.render_items) {
                const auto imported = assets.import_mesh(item.mesh_handle);
                ASSERT_TRUE(imported) << imported.error().message;
                const auto geometry = assets.ensure_loaded(item.mesh_handle, AssetType::Mesh);
                ASSERT_TRUE(geometry) << geometry.error().message;
                const auto surface =
                    assets.ensure_loaded(item.material_handle, AssetType::Material);
                ASSERT_TRUE(surface) << surface.error().message;
            }
            if(scene.environment.asset) {
                const auto environment = assets.load_environment(scene.environment.asset);
                ASSERT_TRUE(environment) << environment.error().message;
            }
            SceneResolver resolver(engine->get_asset_registry());
            const auto world = resolver.update(scene);
            ASSERT_EQ(world->render_items.size(), scene.render_items.size());
            builder.reset();
            const auto captured = builder.update(
                *world, [&](AssetHandle handle) { return assets.read_mesh_data(handle); },
                [&](AssetHandle handle) { return assets.mesh_data_revision(handle); },
                {.texture = [&](AssetHandle handle) { return assets.read_texture_data(handle); },
                    .environment =
                        [&](AssetHandle handle) {
                            return assets.read_environment_background(handle);
                        },
                    .revision =
                        [&](AssetHandle handle) {
                            return assets.get_database().get_revision(handle);
                        }});
            ASSERT_TRUE(captured) << captured.error();
            view.path_trace = captured.value();
        }
        uint32_t samples() const {
            return engine->get_renderer().get_scene_renderer().get_path_trace_statistics().samples;
        }
    };

    TEST_F(PathTraceGpuTest, AccumulationStopsAtTargetAndResetsOnlyWhenNeeded) {
        source.render_items.clear();
        source.environment.background_color = {.25f, .5f, .75f};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        view.path_trace_settings.samples = 3;
        ASSERT_NO_FATAL_FAILURE(draw(5));
        EXPECT_EQ(samples(), 3u);
        auto first = pixels();
        ASSERT_EQ(first.size(), SIZE.x * SIZE.y);
        for(const auto pixel : first)
            EXPECT_EQ(pixel, Math::Vec4(.25f, .5f, .75f, 1));
        scene.post_process.exposure = 2;
        view.path_trace_settings.samples = 5;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 4u);
        EXPECT_EQ(pixels(), first);
        scene.cameras[0].view_matrix[3].x = .25f;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        view.path_trace_settings.seed = 7;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        source.environment.background_color = {.5f, .2f, .1f};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(pixels()[0], Math::Vec4(.5f, .2f, .1f, 1));
        view.render_size = {64, 32};
        ASSERT_TRUE(engine->get_renderer().enable_offscreen_rendering(view.render_size));
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(engine->get_renderer().get_scene_renderer().get_path_trace_statistics().size,
            Math::Vec2u(32, 16));
        const auto snapshot = view.path_trace;
        view.path_trace.reset();
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 0u);
        view.path_trace = snapshot;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
    }

    TEST_F(PathTraceGpuTest, UnlitEmissionAndImageOrientationMatchTheCamera) {
        material = std::make_shared<Material>("emitter", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {1, .2f, 0, 1}));
        ASSERT_TRUE(material->set_scalar_property("intensity", 2));
        source.render_items[0].material.resource = material;
        mesh.vertices = {{{-.8f, .1f, 0}, {}, {0, 0, 1}}, {{.8f, .1f, 0}, {}, {0, 0, 1}},
            {{0, .95f, 0}, {}, {0, 0, 1}}};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(4));
        const auto image = pixels();
        ASSERT_EQ(image.size(), SIZE.x * SIZE.y);
        EXPECT_EQ(image[8 * SIZE.x + 16], Math::Vec4(2, .4f, 0, 1));
        EXPECT_EQ(image[24 * SIZE.x + 16], Math::Vec4(0, 0, 0, 1));
    }

    TEST_F(PathTraceGpuTest, AdditionalBouncesCarryIndirectLightAndSamplesConverge) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .2f, .1f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        source.environment.background_color = {1, 1, 1};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(pixels()[SIZE.x * 16 + 16], Math::Vec4(0, 0, 0, 1));
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto one = pixels();
        ASSERT_NO_FATAL_FAILURE(draw(15));
        const auto sixteen = pixels();
        ASSERT_NO_FATAL_FAILURE(draw(112));
        const auto reference = pixels();
        ASSERT_EQ(samples(), 128u);
        double error_one = 0, error_sixteen = 0;
        Math::Vec3 mean{};
        for(size_t i = 0; i < reference.size(); ++i) {
            const auto a = Math::Vec3(one[i] - reference[i]);
            const auto b = Math::Vec3(sixteen[i] - reference[i]);
            error_one += Math::dot(a, a);
            error_sixteen += Math::dot(b, b);
            mean += Math::Vec3(reference[i]) / float(reference.size());
            EXPECT_TRUE(Math::is_finite(reference[i]));
        }
        EXPECT_LT(error_sixteen, error_one * .2);
        EXPECT_GT(mean.r, .45f);
        EXPECT_LT(mean.r, .7f);
        EXPECT_GT(mean.r, mean.g * 2);
        EXPECT_GT(mean.g, mean.b * 1.5f);
    }

    TEST_F(PathTraceGpuTest, SceneSettingsEnableTheSharedRuntimePathWithoutEditorOverrides) {
        view.path_trace.reset();
        view.path_trace_enabled = false;
        engine->get_renderer().set_ray_scene_sources(
            [&](AssetHandle) { return Result<MeshData, Error>::success(mesh); }, {});
        scene.environment.background_color = {2, 1, .5f};
        scene.post_process.path_trace = {
            .samples = 4, .max_dimension = 32, .enabled = true, .dynamic = true, .denoise = true};
        ASSERT_NO_FATAL_FAILURE(draw(3));
        EXPECT_EQ(samples(), 1u) << engine->get_renderer().path_trace_error();
        const auto result = mean(pixels());
        EXPECT_LT(Math::length(result - Math::Vec3(2, 1, .5f)), 1e-5f);
        scene.post_process.path_trace.enabled = false;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 0u);
    }

    TEST_F(PathTraceGpuTest, DynamicDenoisingReducesNoiseAndSurvivesRefitResizeAndReset) {
        source.environment.background_color = {1, 1, 1};
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        ASSERT_TRUE(material->set_vector_property("base_color", {.6f, .2f, .1f, 1}));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto reference = pixels();
        view.path_trace_settings.dynamic = true;
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto noisy = pixels();
        view.path_trace_settings.denoise = true;
        ASSERT_NO_FATAL_FAILURE(draw(8));
        const auto filtered = pixels();
        double original_error = 0, filtered_error = 0;
        for(size_t i = 0; i < filtered.size(); ++i) {
            ASSERT_TRUE(Math::is_finite(filtered[i]));
            const auto a = Math::Vec3(noisy[i] - reference[i]);
            const auto b = Math::Vec3(filtered[i] - reference[i]);
            original_error += Math::dot(a, a);
            filtered_error += Math::dot(b, b);
        }
        EXPECT_LT(filtered_error, original_error * .4);
        EXPECT_EQ(samples(), 1u);
        source.render_items[0].model_matrix = Math::translate(Math::Mat4(1), {0, 0, -.5f});
        ASSERT_NO_FATAL_FAILURE(capture_source(false));
        ASSERT_NO_FATAL_FAILURE(draw(2));
        view.render_size = {64, 32};
        ++view.path_trace_settings.reset_generation;
        ASSERT_TRUE(engine->get_renderer().enable_offscreen_rendering(view.render_size));
        ASSERT_NO_FATAL_FAILURE(draw(2));
        for(const auto value : pixels())
            EXPECT_TRUE(Math::is_finite(value));
        view.path_trace_settings.denoise = false;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
    }

    TEST_F(PathTraceGpuTest, MediumAbsorptionUsesThicknessAndNestedVolumesReplaceTheirParent) {
        source.environment.background_color = {1, 1, 1};
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("transmission", 1));
        ASSERT_TRUE(material->set_scalar_property("ior", 1));
        ASSERT_TRUE(material->set_vector_property("attenuation_color", {.25f, .25f, .25f, 1}));
        mesh.vertices.insert(
            mesh.vertices.end(), {{{-20, -20, -1}, {}, {0, 0, -1}}, {{0, 20, -1}, {}, {0, 0, -1}},
                                     {{20, -20, -1}, {}, {0, 0, -1}}});
        mesh.indices = {0, 1, 2, 3, 4, 5};
        view.path_trace_settings.bounces = 8;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto absorbed = mean(pixels());
        EXPECT_NEAR(absorbed.r, .25f, .001f);
        auto inner = std::make_shared<Material>("inner", "pbr");
        ASSERT_TRUE(inner->set_scalar_property("transmission", 1));
        ASSERT_TRUE(inner->set_scalar_property("ior", 1));
        ASSERT_TRUE(inner->set_vector_property("base_color", {1, 1, 1, 1}));
        auto item = source.render_items[0];
        item.material.resource = inner;
        item.model_matrix =
            Math::scale(Math::translate(Math::Mat4(1), {0, 0, -.25f}), Math::Vec3(1, 1, .5f));
        source.render_items.push_back(item);
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        EXPECT_NEAR(mean(pixels()).r, .5f, .006f);
    }

    TEST_F(PathTraceGpuTest, HomogeneousAbsorbingMediumAttenuatesFiniteDistance) {
        material = std::make_shared<Material>("emitter", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {1, 1, 1, 1}));
        source.render_items[0].material.resource = material;
        view.path_trace_settings.medium_density = .5f;
        view.path_trace_settings.medium_color = Math::Vec3(0);
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto result = mean(pixels());
        EXPECT_NEAR(result.r, std::exp(-.5f * 2.9f), .03f);
        EXPECT_NEAR(result.g, result.r, 1e-5f);
    }

    TEST_F(PathTraceGpuTest, RefractionBendsRaysAndRoughnessBroadensTransmission) {
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("transmission", 1));
        ASSERT_TRUE(material->set_scalar_property("ior", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", .045f));
        mesh.vertices = {{{-3, -3, 0}, {0, 0}, {0, 0, 1}}, {{3, -3, 0}, {1, 0}, {0, 0, 1}},
            {{0, 3, 0}, {.5f, 1}, {0, 0, 1}}};
        source.render_items[0].model_matrix =
            Math::rotate(Math::Mat4(1), Math::radians(30.f), Math::Vec3(0, 1, 0));
        auto target = std::make_shared<Material>("target", "pbr");
        ASSERT_TRUE(target->set_vector_property("base_color", {0, 0, 0, 1}));
        ASSERT_TRUE(target->set_vector_property("emissive_color", {1, 1, 1, 1}));
        TextureData texture{.width = 2,
            .height = 1,
            .format = Format::R8G8B8A8_UNORM,
            .pixels = {255, 0, 0, 255, 0, 0, 255, 255},
            .sampler = SamplerDesc{.mag_filter = Filter::Nearest, .min_filter = Filter::Nearest}};
        auto gpu = engine->get_render_resource_factory().try_create_texture(texture);
        ASSERT_TRUE(gpu);
        images[AssetHandle(20)] = texture;
        target->set_texture_property("emissive_texture", gpu.value(), AssetHandle(20));
        auto plane = source.render_items[0];
        plane.material.resource = target;
        plane.model_matrix = Math::translate(Math::Mat4(1), {0, 0, -2});
        source.render_items.push_back(plane);
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_GT(pixels()[16 * SIZE.x + 16].b, .9f);
        ASSERT_TRUE(material->set_scalar_property("ior", 1.5f));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto clear = pixels()[16 * SIZE.x + 16];
        EXPECT_GT(clear.r, .25f);
        EXPECT_LT(clear.b, .03f);
        ASSERT_TRUE(material->set_scalar_property("roughness", .6f));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto rough = pixels()[16 * SIZE.x + 16];
        EXPECT_TRUE(Math::is_finite(rough));
        EXPECT_GT(rough.b, clear.b + .025f);
    }

    TEST_F(PathTraceGpuTest, IndexMatchedTransmissionPassesThroughRegardlessOfRoughness) {
        source.environment.background_color = {2, 1, .5f};
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("transmission", 1));
        ASSERT_TRUE(material->set_scalar_property("ior", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", .7f));
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(mean(pixels()), Math::Vec3(2, 1, .5f));
    }

    TEST_F(PathTraceGpuTest, EnteringAndLeavingClosedGlassConservesConstantEnvironmentRadiance) {
        source.environment.background_color = {2, 1, .5f};
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("transmission", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", .045f));
        mesh.vertices.insert(
            mesh.vertices.end(), {{{-20, -20, -1}, {}, {0, 0, -1}}, {{0, 20, -1}, {}, {0, 0, -1}},
                                     {{20, -20, -1}, {}, {0, 0, -1}}});
        mesh.indices = {0, 1, 2, 3, 4, 5};
        view.path_trace_settings.bounces = 8;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        auto image = pixels();
        for(const auto pixel : image)
            EXPECT_TRUE(Math::is_finite(pixel));
        const auto color = mean(image);
        EXPECT_NEAR(color.r, 2, .06);
        EXPECT_NEAR(color.g, 1, .03);
        EXPECT_NEAR(color.b, .5, .015);
    }

    TEST_F(PathTraceGpuTest, InsideMediumUsesCriticalAngleAndTotalInternalReflection) {
        source.environment.background_color = {1, 1, 1};
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("transmission", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", .045f));
        mesh.indices = {0, 2, 1};
        for(auto& vertex : mesh.vertices)
            vertex.normal = {0, 0, -1};
        scene.cameras[0].view_matrix =
            Math::look_at(Math::Vec3(0, 0, 1), Math::Vec3(1.7320508f, 0, 0), Math::Vec3(0, 1, 0));
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto reflected = mean(pixels());
        EXPECT_NEAR(reflected.r, 1, .03);
        ASSERT_TRUE(material->set_scalar_property("ior", 1.1f));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto transmitted = mean(pixels());
        EXPECT_GT(transmitted.r, reflected.r + .12f);
        EXPECT_LT(transmitted.r, 1.25f);
    }

    TEST_F(PathTraceGpuTest, ExportReadsNativeLinearPixelsAndPreservesAccumulationAndLayout) {
        auto& renderer = engine->get_renderer();
        EXPECT_FALSE(renderer.capture_path_trace());
        source.environment.background_color = {0, 0, 2};
        ASSERT_TRUE(material->set_vector_property("emissive_color", {4, 0, 0, 1}));
        ASSERT_TRUE(material->set_scalar_property("emissive_strength", 1));
        mesh.vertices = {
            {{-1, 0, 0}, {}, {0, 0, 1}}, {{1, 0, 0}, {}, {0, 0, 1}}, {{0, 1, 0}, {}, {0, 0, 1}}};
        scene.post_process.exposure = 2;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto expected = pixels();
        auto captured = renderer.capture_path_trace();
        ASSERT_TRUE(captured) << captured.error().message;
        EXPECT_EQ(captured.value().format, Format::R32G32B32A32_SFLOAT);
        EXPECT_EQ(captured.value().width, SIZE.x);
        EXPECT_EQ(captured.value().height, SIZE.y);
        const auto decoded = CpuTextureImage::decode(captured.value(), 1024 * 1024);
        ASSERT_TRUE(decoded);
        EXPECT_EQ(decoded.value().pixels, expected);
        EXPECT_GT(mean(expected).r, 0);
        EXPECT_GT(mean(expected).b, 0);
        EXPECT_EQ(samples(), 1u);
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 2u);
        EXPECT_FALSE(renderer.capture_path_trace(9999));
    }

    TEST_F(PathTraceGpuTest, AlphaMaskUsesFactorAndTextureAndOpaqueIgnoresAlpha) {
        source.environment.background_color = {0, 0, 1};
        ASSERT_TRUE(material->set_scalar_property("emissive_strength", 1));
        ASSERT_TRUE(material->set_vector_property("emissive_color", {1, 0, 0, 1}));
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, .5f}));
        ASSERT_TRUE(material->set_surface({.alpha_mode = AlphaMode::Mask, .alpha_cutoff = .25f}));
        set_texture("base_color_texture", AssetHandle(20),
            {.width = 2,
                .height = 1,
                .format = Format::R8G8B8A8_UNORM,
                .pixels = {255, 255, 255, 64, 255, 255, 255, 255}});
        view.path_trace_settings.bounces = 1;
        for(auto& vertex : mesh.vertices)
            vertex.texcoord = {.25f, .5f};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(mean(pixels()), Math::Vec3(0, 0, 1));
        for(auto& vertex : mesh.vertices)
            vertex.texcoord = {.75f, .5f};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(mean(pixels()), Math::Vec3(1, 0, 0));
        ASSERT_TRUE(material->set_surface({.alpha_mode = AlphaMode::Opaque}));
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 0}));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(mean(pixels()), Math::Vec3(1, 0, 0));
    }

    TEST_F(PathTraceGpuTest, CutoutBlockersDoNotCastSolidShadows) {
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        source.lights = {{.type = LightType::Directional,
            .direction = Math::normalize(Math::Vec3(-1, 0, -1)),
            .intensity = 10}};
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto clear = pixels()[16 * SIZE.x + 16];
        auto cutout = std::make_shared<Material>("cutout", "pbr");
        ASSERT_TRUE(cutout->set_surface({.alpha_mode = AlphaMode::Mask, .double_sided = true}));
        ASSERT_TRUE(cutout->set_vector_property("base_color", {1, 1, 1, 0}));
        auto blocker = source.render_items[0];
        blocker.material.resource = cutout;
        blocker.model_matrix =
            Math::translate(Math::Mat4(1), {1, 0, 1}) * Math::scale(Math::Mat4(1), {.02f, .05f, 1});
        source.render_items.push_back(blocker);
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(pixels()[16 * SIZE.x + 16], clear);
        ASSERT_TRUE(cutout->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_LT(pixels()[16 * SIZE.x + 16].r, .001f);
        EXPECT_GT(clear.r, .1f);
    }

    TEST_F(PathTraceGpuTest, BlendCoverageConvergesWithoutSpendingBouncesOnHoles) {
        source.environment.background_color = {0, 0, 1};
        material = std::make_shared<Material>("blend", "unlit_color");
        ASSERT_TRUE(material->set_surface({.alpha_mode = AlphaMode::Blend}));
        ASSERT_TRUE(material->set_vector_property("color", {1, 0, 0, .25f}));
        source.render_items[0].material.resource = material;
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto color = mean(pixels());
        EXPECT_NEAR(color.r, .25f, .01f);
        EXPECT_NEAR(color.b, .75f, .01f);
        EXPECT_FLOAT_EQ(color.g, 0);
    }

    TEST_F(PathTraceGpuTest, AnalyticLightsUseBvhVisibility) {
        ASSERT_TRUE(material->set_vector_property("base_color", {.8f, .8f, .8f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        ASSERT_TRUE(material->set_surface({.double_sided = true}));
        for(auto type : {LightType::Directional, LightType::Point, LightType::Spot}) {
            source.lights = {{.type = type,
                .position = {2, 0, 2},
                .direction = Math::normalize(Math::Vec3(-1, 0, -1)),
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
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_EQ(view.path_trace->emitters->size(), 1u);
        ASSERT_NO_FATAL_FAILURE(draw(64));
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
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(64));
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

    TEST_F(PathTraceGpuTest, TexturedPbrMatchesEquivalentConstantParameters) {
        const TextureData color{.width = 1,
            .height = 1,
            .format = Format::R8G8B8A8_SRGB,
            .pixels = {128, 200, 64, 255}};
        const TextureData mr{.width = 1, .height = 1, .pixels = {20, 128, 192, 255}};
        set_texture("base_color_texture", AssetHandle(10), color);
        set_texture("emissive_texture", AssetHandle(10), color);
        set_texture("metallic_roughness_texture", AssetHandle(11), mr);
        ASSERT_TRUE(material->set_vector_property("base_color", {.8f, .7f, .6f, 1}));
        ASSERT_TRUE(material->set_vector_property("emissive_color", {.1f, .2f, .3f, 1}));
        ASSERT_TRUE(material->set_scalar_property("emissive_strength", 2));
        ASSERT_TRUE(material->set_scalar_property("metallic", .7f));
        ASSERT_TRUE(material->set_scalar_property("roughness", .8f));
        source.lights = {{.type = LightType::Directional, .direction = {0, 0, -1}, .intensity = 1}};
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto textured = pixels();
        const auto decoded = CpuTextureImage::decode(color, 16).value().pixels[0];
        material->set_texture_property("base_color_texture", nullptr);
        material->set_texture_property("emissive_texture", nullptr);
        material->set_texture_property("metallic_roughness_texture", nullptr);
        ASSERT_TRUE(
            material->set_vector_property("base_color", decoded * Math::Vec4(.8f, .7f, .6f, 1)));
        ASSERT_TRUE(material->set_vector_property(
            "emissive_color", decoded * Math::Vec4(.1f, .2f, .3f, 1)));
        ASSERT_TRUE(material->set_scalar_property("metallic", .7f * 192 / 255.f));
        ASSERT_TRUE(material->set_scalar_property("roughness", .8f * 128 / 255.f));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto equivalent = pixels();
        ASSERT_EQ(textured.size(), equivalent.size());
        for(size_t i = 0; i < textured.size(); ++i)
            for(int axis = 0; axis < 3; ++axis)
                EXPECT_NEAR(textured[i][axis], equivalent[i][axis], 1e-5f);
    }

    TEST_F(PathTraceGpuTest, TextureAddressingAndFilteringMatchCpuSampling) {
        ASSERT_TRUE(material->set_vector_property("emissive_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("emissive_strength", 1));
        view.path_trace_settings.bounces = 1;
        TextureData image{.width = 2,
            .height = 2,
            .pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}};
        for(const auto filter : {Filter::Linear, Filter::Nearest}) {
            for(const auto address : {SamplerAddressMode::Repeat, SamplerAddressMode::MirrorRepeat,
                    SamplerAddressMode::ClampToEdge, SamplerAddressMode::ClampToBorder}) {
                image.sampler = SamplerDesc{
                    .mag_filter = filter, .address_mode_u = address, .address_mode_v = address};
                ASSERT_NO_FATAL_FAILURE(set_texture("emissive_texture", AssetHandle(10), image));
                const auto decoded = CpuTextureImage::decode(image, 64).value();
                for(const Math::Vec2 uv : {Math::Vec2(-.2f, 1.1f), Math::Vec2(1, 1)}) {
                    for(auto& vertex : mesh.vertices)
                        vertex.texcoord = uv;
                    ASSERT_NO_FATAL_FAILURE(capture_source());
                    ASSERT_NO_FATAL_FAILURE(draw());
                    const auto actual = mean(pixels());
                    const auto expected = Math::Vec3(decoded.sample(uv));
                    for(int axis = 0; axis < 3; ++axis)
                        EXPECT_NEAR(actual[axis], expected[axis], 2e-5f);
                }
            }
        }
    }

    TEST_F(PathTraceGpuTest, NormalMapUsesSourceTangentsAndUvFallbackWithAdjustableScale) {
        set_texture("normal_texture", AssetHandle(10),
            {.width = 1, .height = 1, .pixels = {230, 128, 204, 255}});
        source.lights = {{.type = LightType::Directional, .direction = {0, 0, -1}, .intensity = 1}};
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        mesh.vertices[0].texcoord = {0, 0};
        mesh.vertices[1].texcoord = {1, 0};
        mesh.vertices[2].texcoord = {.5f, 1};
        for(auto& vertex : mesh.vertices)
            vertex.tangent = {1, 0, 0, 1};
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto mapped = pixels();
        for(auto& vertex : mesh.vertices)
            vertex.tangent = {};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto fallback = pixels();
        EXPECT_NEAR(mean(mapped).r, mean(fallback).r, 1e-5f);
        ASSERT_TRUE(material->set_scalar_property("normal_scale", 0));
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        const auto flat = mean(pixels());
        EXPECT_LT(mean(mapped).r, flat.r * .85f);
        EXPECT_GT(mean(mapped).r, flat.r * .4f);
    }

    TEST_F(PathTraceGpuTest, HdrBackgroundHonorsVisibilityIntensityAndRotation) {
        source.render_items.clear();
        images[AssetHandle(12)] = constant_cube({Math::Vec3(1, 0, 0), Math::Vec3(0, 1, 0),
            Math::Vec3(0), Math::Vec3(0), Math::Vec3(0), Math::Vec3(0, 0, 1)});
        source.environment = {.asset = AssetHandle(12),
            .background = true,
            .intensity = 2,
            .lighting = true,
            .lighting_intensity = 4,
            .background_color = {.1f, .2f, .3f}};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        auto first = pixels();
        EXPECT_NEAR(first[16 * 32 + 16].b, 2, 1e-5f);
        source.environment.rotation = 90;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        auto rotated = pixels();
        EXPECT_EQ(samples(), 1u);
        EXPECT_NEAR(rotated[16 * 32 + 16].r, 2, 1e-5f);
        EXPECT_NEAR(rotated[16 * 32 + 16].b, 0, 1e-5f);
        source.environment.background = false;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw());
        for(const auto pixel : pixels())
            EXPECT_EQ(pixel, Math::Vec4(.1f, .2f, .3f, 1));
    }

    TEST_F(PathTraceGpuTest, EnvironmentMisPreservesEnergyAtTheLastBounceAndSeparatesLighting) {
        images[AssetHandle(12)] = constant_cube({Math::Vec3(1), Math::Vec3(1), Math::Vec3(1),
            Math::Vec3(1), Math::Vec3(1), Math::Vec3(1)});
        source.environment = {.asset = AssetHandle(12), .lighting = true};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto direct = mean(pixels());
        EXPECT_GT(direct.r, .7f);
        EXPECT_LT(direct.r, .85f);
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto mis = mean(pixels());
        EXPECT_NEAR(mis.r, direct.r, .025f);
        source.environment.lighting_intensity = 2;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(128));
        EXPECT_NEAR(mean(pixels()).r, mis.r * 2, .01f);
        source.environment.lighting = false;
        source.environment.background = true;
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(2));
        EXPECT_EQ(mean(pixels()), Math::Vec3(0));
    }

    TEST_F(PathTraceGpuTest, UnevenEnvironmentImportanceSamplingMatchesBsdfMis) {
        images[AssetHandle(12)] = constant_cube({Math::Vec3(0), Math::Vec3(0), Math::Vec3(0),
            Math::Vec3(0), Math::Vec3(8, 2, 1), Math::Vec3(0)});
        source.environment = {.asset = AssetHandle(12), .lighting = true};
        ASSERT_NO_FATAL_FAILURE(capture_source());
        view.path_trace_settings.bounces = 1;
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto direct = mean(pixels());
        EXPECT_GT(direct.r, 1);
        EXPECT_NEAR(direct.r, direct.g * 4, .02f);
        view.path_trace_settings.bounces = 2;
        ASSERT_NO_FATAL_FAILURE(draw(128));
        const auto mis = mean(pixels());
        for(int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(mis[axis], direct[axis], direct[axis] * .04f);
    }

    TEST_F(PathTraceGpuTest, UnsubmittedRecordingCannotBecomeCompletedAccumulation) {
        capture_source();
        auto& device = engine->get_renderer().get_render_context().get_device();
        PathTracePass pass(device);
        FrameWait wait{device, *copy_frames};
        auto settings = view.path_trace_settings;
        settings.samples = 1;
        const ViewProjectMatrix camera{Math::Mat4(1), Math::Mat4(1)};
        const auto display = [](RenderGraph::PassId, CommandBuffer&) {
            return Result<void, GraphicsError>::success();
        };
        begin_frame(*copy_frames);
        ASSERT_TRUE(pass.render(*copy_frames, view.path_trace, camera, settings, SIZE, display));
        const auto unsubmitted = pass.output();
        ASSERT_EQ(pass.statistics().samples, 1u);
        ASSERT_TRUE(pass.render(*copy_frames, view.path_trace, camera, settings, SIZE, display));
        EXPECT_NE(pass.output(), unsubmitted);
        EXPECT_EQ(pass.statistics().samples, 1u);
        EXPECT_EQ(pass.statistics().dispatches, 1u);
        submit(device, *copy_frames);
        begin_frame(*copy_frames);
        ASSERT_TRUE(pass.render(*copy_frames, view.path_trace, camera, settings, SIZE, display));
        EXPECT_EQ(pass.statistics().dispatches, 1u);
        submit(device, *copy_frames);
        copy_frames->wait_for_all_slots();
    }

    TEST_F(PathTraceGpuTest, ReusesSceneBuffersForResizeResampleLightingAndMaterialChanges) {
        ASSERT_NO_FATAL_FAILURE(capture_source());
        ASSERT_NO_FATAL_FAILURE(draw(2));
        const auto stats = [&] {
            return engine->get_renderer().get_scene_renderer().get_path_trace_statistics();
        };
        const auto initial = stats();
        const auto geometry = view.path_trace->geometry;
        ++view.path_trace_settings.reset_generation;
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(stats().upload_bytes, initial.upload_bytes);
        view.render_size = {64, 32};
        ASSERT_TRUE(engine->get_renderer().enable_offscreen_rendering(view.render_size));
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(samples(), 1u);
        EXPECT_EQ(stats().upload_bytes, initial.upload_bytes);
        source.lights.push_back({.direction = {0, 0, -1}, .intensity = 1});
        ASSERT_NO_FATAL_FAILURE(capture_source(false));
        EXPECT_EQ(view.path_trace->geometry, geometry);
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(stats().geometry_upload_bytes, initial.geometry_upload_bytes);
        const auto lit = stats();
        ASSERT_TRUE(material->set_scalar_property("roughness", .8f));
        ASSERT_NO_FATAL_FAILURE(capture_source(false));
        ASSERT_NO_FATAL_FAILURE(draw());
        EXPECT_EQ(stats().geometry_upload_bytes, initial.geometry_upload_bytes);
        EXPECT_GT(stats().upload_bytes, lit.upload_bytes);
        view.path_trace_settings.samples = 1;
        const auto dispatches = stats().dispatches;
        ASSERT_NO_FATAL_FAILURE(draw(3));
        EXPECT_EQ(stats().dispatches, dispatches);
    }

    TEST_F(PathTraceGpuTest, MaterialReferenceUsesImportedTexturesAndEnvironmentProducts) {
        ASSERT_NO_FATAL_FAILURE(capture_project_scene("scenes/path_tracing_materials.scene"));
        EXPECT_GT(view.path_trace->materials->images->textures.size(), 3u);
        EXPECT_TRUE(std::ranges::any_of(view.path_trace->materials->surfaces,
            [](const auto& surface) { return surface.opacity.z == float(AlphaMode::Mask); }));
        EXPECT_TRUE(std::ranges::any_of(view.path_trace->materials->surfaces,
            [](const auto& surface) { return surface.opacity.z == float(AlphaMode::Blend); }));
        EXPECT_GT(view.path_trace->environment_map->size.x, 0u);
        view.path_trace_settings.bounces = 3;
        ASSERT_NO_FATAL_FAILURE(draw(32));
        const auto image = pixels();
        for(const auto pixel : image)
            EXPECT_TRUE(Math::is_finite(pixel));
        EXPECT_GT(mean(image).r + mean(image).g + mean(image).b, .01f);
        engine->get_renderer().wait_idle();
    }

    TEST_F(PathTraceGpuTest, ReferenceRoomUsesPublishedMeshProductsAndMaterialSnapshots) {
        ASSERT_NO_FATAL_FAILURE(capture_project_scene("scenes/path_tracing.scene"));
        ASSERT_GT(view.path_trace->geometry->triangles.size(), 50u);
        ASSERT_GT(view.path_trace->emitters->size(), 0u);
        view.path_trace_settings.bounces = 4;
        ASSERT_NO_FATAL_FAILURE(draw(64));
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
