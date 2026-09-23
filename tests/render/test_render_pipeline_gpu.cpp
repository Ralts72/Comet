#include "support/render_graph_gpu_fixture.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "unlit_color_vert.h"

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, ProjectShaderProgramChangesPixelsAfterCpuVersionReplacement) {
        constexpr AssetHandle program_handle(9811);
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
        auto& scene = renderer.get_scene_renderer();
        TemporaryDirectory sources;
        auto& registry = engine->get_asset_registry();
        const auto compile_program = [&](const float scale) {
            const auto path = sources.path() / "project.frag";
            const std::string fragment =
                "#version 450\n"
                "layout(location=0) out vec4 color;\n"
                "layout(set=1,binding=0,std140) uniform MaterialData {\n"
                "  vec4 color; float intensity;\n"
                "} material;\n"
                "void main() { color = vec4(material.color.rgb * material.intensity * "
                + std::to_string(scale) + ", material.color.a); }\n";
            EXPECT_TRUE(write_text_file_atomic(path, fragment));
            auto compiled =
                ShaderCompiler::compile({.source = path, .stage = ShaderStage::Fragment});
            EXPECT_TRUE(compiled.succeeded()) << compiled.diagnostics;
            auto program = std::make_shared<ShaderProgramArtifact>();
            program->handle = program_handle;
            program->vertex_words.assign(UNLIT_COLOR_VERT.begin(), UNLIT_COLOR_VERT.end());
            program->fragment_words = std::move(compiled.words);
            return program;
        };
        ASSERT_TRUE(registry.register_asset(program_handle, compile_program(0.25f)));
        auto material = std::make_shared<Material>("project", "unlit_color", program_handle);
        ASSERT_TRUE(material->set_vector_property("color", {0.8f, 0.4f, 0.2f, 1}));
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(9812), material}}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1)
                ASSERT_TRUE(registry.replace_asset(program_handle, compile_program(0.5f)));
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())->get_image(),
                outputs[index], {4, 4});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const auto format = scene.get_offscreen_color_view(0)->get_image()->get_info().format;
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        for(std::size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            const float scale = index == 0 ? 0.25f : 0.5f;
            for(std::size_t channel = 0; channel < 3; ++channel) {
                const float base = channel == 0 ? 0.8f : channel == 1 ? 0.4f : 0.2f;
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(bytes[(2 * 4 + 2) * 4 + component]),
                    mapped_byte(base * scale), 3);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrParametersAndCameraProjectionMatchReferencePixels) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({33, 33}));
        auto& scene = renderer.get_scene_renderer();
        auto material = std::make_shared<Material>("pbr", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.8f, 0.2f, 0.1f, 1}));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        struct Scenario {
            const char* name;
            float metallic = 0;
            float roughness = 0.6f;
            Math::Vec3 camera{0, 0, 3};
            bool orthographic = false;
            LightType light_type = LightType::Directional;
            Math::Vec3 normal{0, 0, 1};
            Math::Vec3 light_direction{0.5f, 0, -1};
            float intensity = 4;
            bool has_light = true;
        };
        for(const auto& sample :
            {Scenario{.name = "dielectric"}, Scenario{.name = "metal", .metallic = 1},
                Scenario{.name = "mixed smooth", .metallic = 0.5f, .roughness = 0.2f},
                Scenario{.name = "lower parameter bounds", .metallic = -1, .roughness = -1},
                Scenario{.name = "upper parameter bounds", .metallic = 2, .roughness = 2},
                Scenario{.name = "translated camera", .camera = {1, 0, 3}},
                Scenario{.name = "orthographic", .orthographic = true},
                Scenario{.name = "point light",
                    .light_type = LightType::Point,
                    .intensity = 4 * Math::PI},
                Scenario{.name = "spot light",
                    .light_type = LightType::Spot,
                    .light_direction = {0, 0, -1},
                    .intensity = 4 * Math::PI},
                Scenario{.name = "zero normal", .normal = {}},
                Scenario{.name = "back-facing normal", .normal = {0, 0, -1}},
                Scenario{.name = "intense smooth metal",
                    .metallic = 1,
                    .roughness = 0.045f,
                    .light_direction = {0, 0, -1},
                    .intensity = 10000},
                Scenario{.name = "no lights", .has_light = false}}) {
            SCOPED_TRACE(sample.name);
            ASSERT_TRUE(material->set_scalar_property("metallic", sample.metallic));
            ASSERT_TRUE(material->set_scalar_property("roughness", sample.roughness));
            auto projection = Math::perspective(60, 1, 0.1f, 10);
            if(sample.orthographic)
                projection = Math::ortho(-1, 1, -1, 1, 0.1f, 10);
            RenderLight light{.type = sample.light_type,
                .position = {0, 0, 2.5f},
                .direction = sample.light_direction,
                .intensity = sample.intensity};
            RenderSubmission submission{
                .view_project_matrix =
                    ViewProjectMatrix{
                        Math::look_at(sample.camera, {0, 0, 0.5f}, {0, 1, 0}), projection},
                .render_items = {{.mesh = lit_quad(sample.normal),
                    .material = {AssetHandle(781), material}}},
                .lights = {light}};
            if(!sample.has_light)
                submission.lights.clear();
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            copy_output(frames, view->get_image(), output, {33, 33});
            submit(device, frames, drawn.value());
            frames.wait_for_all_slots();
            auto direction = -glm::dvec3(light.direction);
            double radiance = light.intensity;
            if(light.type != LightType::Directional) {
                direction = {0, 0, 1};
                radiance *= std::pow(1 - std::pow(2.0 / light.range, 4), 2) / 4;
            }
            if(!sample.has_light)
                radiance = 0;
            const auto expected = pbr_reference(glm::dvec3(sample.normal),
                glm::dvec3(sample.camera) - glm::dvec3(0, 0, 0.5), direction, {0.8, 0.2, 0.1},
                sample.metallic, sample.roughness, radiance);
            const auto bytes = output->read();
            const auto format = view->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            for(unsigned x : {4u, 16u, 28u}) {
                if(!sample.orthographic && x != 16)
                    continue;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[(16 * 33 + x) * 4 + component]),
                        mapped_byte(static_cast<float>(expected[channel])), 3);
                }
            }
        }
    }

    TEST_F(RenderGraphGpuTest, IblLightsDielectricsAndMetalsWithoutBackgroundAndKeepsOldFrames) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({9, 9}));
        auto& scene = renderer.get_scene_renderer();
        TemporaryDirectory directory;
        write_hdr(directory.path() / "uniform.hdr");
        auto data = EnvironmentImporter{}.import(directory.path() / "uniform.hdr");
        ASSERT_TRUE(data);
        auto uploaded = Environment::try_create(engine->get_render_resources(), data.value());
        ASSERT_TRUE(uploaded);
        auto environment = std::move(uploaded).value();
        const auto mesh = lit_quad();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        struct Scenario {
            float metallic = 0;
            float roughness = 0.25f;
            bool enabled = true;
            bool background = false;
            float intensity = 1;
            bool missing = false;
            bool replace = false;
            float rotation = 0;
        };
        const std::array cases{Scenario{}, Scenario{.metallic = 1}, Scenario{.roughness = 1},
            Scenario{.metallic = 1, .roughness = 1}, Scenario{.enabled = false},
            Scenario{.background = true}, Scenario{.intensity = 0.5f}, Scenario{.intensity = 0},
            Scenario{.missing = true}, Scenario{.rotation = 90}, Scenario{.replace = true}};
        std::vector<std::shared_ptr<Readback>> outputs;
        std::vector<glm::dvec3> expected;
        Format output_format{};
        for(size_t index = 0; index < cases.size(); ++index) {
            const auto sample = cases[index];
            if(sample.replace) {
                write_hdr(directory.path() / "uniform.hdr", 16, 8,
                    [](int, int) { return std::array<unsigned char, 4>{32, 64, 128, 131}; });
                auto replacement = EnvironmentImporter{}.import(directory.path() / "uniform.hdr");
                ASSERT_TRUE(replacement);
                auto resource =
                    Environment::try_create(engine->get_render_resources(), replacement.value());
                ASSERT_TRUE(resource);
                environment = std::move(resource).value();
            }
            auto material = std::make_shared<Material>("IBL", "pbr");
            ASSERT_TRUE(material->set_vector_property("base_color", {0.8f, 0.2f, 0.1f, 1}));
            ASSERT_TRUE(material->set_scalar_property("metallic", sample.metallic));
            ASSERT_TRUE(material->set_scalar_property("roughness", sample.roughness));
            RenderSubmission submission{
                .view_project_matrix =
                    ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                        Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
                .render_items = {{.mesh = mesh, .material = {AssetHandle(index + 1000), material}}},
                .environment = {{}, sample.background, 0.1f, sample.rotation, sample.enabled,
                    sample.intensity},
                .environment_resource = environment};
            if(sample.missing)
                submission.environment_resource.reset();
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 9 * 9 * 4);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            output_format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), output, {9, 9});
            submit(device, frames, drawn.value());
            outputs.push_back(output);

            // 在 N=V 时独立积分直接光照的镜面 BRDF，作为半球反射参考值。
            glm::dvec3 reflected(0);
            const glm::dvec3 base(0.8, 0.2, 0.1);
            const auto f0 = glm::mix(glm::dvec3(0.04), base, double(sample.metallic));
            constexpr int STEPS = 8192;
            for(int step = 0; step < STEPS; ++step) {
                const double cosine = (step + 0.5) / STEPS;
                reflected +=
                    pbr_reference({0, 0, 1}, {0, 0, 1}, {std::sqrt(1 - cosine * cosine), 0, cosine},
                        f0, 1, sample.roughness, 1)
                    * (2 * std::numbers::pi / STEPS);
            }
            glm::dvec3 radiance(4, 2, 1);
            if(sample.replace)
                radiance = {1, 2, 4};
            auto color = radiance * (base * (1.0 - sample.metallic) * (1.0 - reflected) + reflected)
                         * double(sample.intensity);
            if(!sample.enabled || sample.missing)
                color = glm::dvec3(0);
            expected.push_back(color);
        }
        frames.wait_for_all_slots();
        const bool bgra =
            output_format == Format::B8G8R8A8_SRGB || output_format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto pixels = outputs[index]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(pixels[(4 * 9 + 4) * 4 + component]),
                    mapped_byte(float(expected[index][channel])), 3)
                    << index << ',' << channel;
            }
        }
    }

    TEST_F(RenderGraphGpuTest, IblRotationChangesReflectionWithBackgroundDisabled) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({9, 9}));
        TemporaryDirectory directory;
        write_hdr(directory.path() / "axes.hdr", 32, 16, [](int x, int) {
            if(x >= 16)
                return std::array<unsigned char, 4>{128, 0, 0, 129};
            return std::array<unsigned char, 4>{0, 0, 128, 129};
        });
        auto data = EnvironmentImporter{}.import(directory.path() / "axes.hdr");
        ASSERT_TRUE(data);
        auto environment = Environment::try_create(engine->get_render_resources(), data.value());
        ASSERT_TRUE(environment);
        auto material = std::make_shared<Material>("mirror", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {1, 1, 1, 1}));
        ASSERT_TRUE(material->set_scalar_property("metallic", 1));
        ASSERT_TRUE(material->set_scalar_property("roughness", 0.045f));
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(1521), material}}},
            .environment = {{}, false, 0, 0, true, 1},
            .environment_resource = environment.value()};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 2> outputs;
        Format format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            submission.environment.rotation = float(index * 180);
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            auto& scene = renderer.get_scene_renderer();
            auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 9 * 9 * 4);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], {9, 9});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto first = outputs[0]->read(), second = outputs[1]->read();
        const auto red = (4 * 9 + 4) * 4 + (bgra ? 2 : 0);
        const auto blue = (4 * 9 + 4) * 4 + (bgra ? 0 : 2);
        EXPECT_GT(std::to_integer<int>(first[red]), std::to_integer<int>(first[blue]) + 100);
        EXPECT_GT(std::to_integer<int>(second[blue]), std::to_integer<int>(second[red]) + 100);
    }

    TEST_F(RenderGraphGpuTest, SkyboxFacesRespectCameraRotationNotTranslationAndKeepOldFrames) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        const Math::Vec2u size(9, 9);
        ASSERT_TRUE(renderer.enable_offscreen_rendering(size));
        TextureData data{.width = 4,
            .height = 4,
            .format = Format::R16G16B16A16_SFLOAT,
            .mip_levels = 3,
            .cubemap = true};
        const std::array<Math::Vec3, 6> colors{
            {{4, 0, 0}, {0, 4, 0}, {0, 0, 4}, {4, 4, 0}, {4, 0, 4}, {0, 4, 4}}};
        for(int extent = 4; extent > 0; extent /= 2)
            for(const auto color : colors)
                for(int i = 0; i < extent * extent; ++i) {
                    const auto packed = glm::packHalf(glm::vec4(color, 1));
                    const auto offset = data.pixels.size();
                    data.pixels.resize(offset + sizeof(packed));
                    std::memcpy(data.pixels.data() + offset, &packed, sizeof(packed));
                }
        auto uploaded = engine->get_render_resources().try_create_texture(data);
        ASSERT_TRUE(uploaded) << uploaded.error().message;
        RenderSubmission submission{.view_project_matrix = ViewProjectMatrix{Math::Mat4(1),
                                        Math::perspective(60.0f, 1.0f, 0.1f, 100.0f)},
            .environment = {{}, true, 0.5f, 0},
            .environment_resource = std::make_shared<Environment>(
                Environment{.background = std::move(uploaded).value()})};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 9> outputs;
        const std::array<Math::Vec3, 9> expected{
            {colors[5] * 0.5f, colors[5] * 0.5f, colors[0] * 0.5f, colors[5] * 0.5f,
                colors[2] * 0.5f, {1, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 1, 0}}};
        auto& scene = renderer.get_scene_renderer();
        Format output_format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1)
                submission.view_project_matrix->view =
                    Math::look_at({19, 4, 8}, {19, 4, 7}, {0, 1, 0});
            if(index == 2)
                submission.environment.rotation = 90;
            if(index == 3) {
                submission.environment.rotation = 0;
                submission.view_project_matrix->projection = Math::ortho(-1, 1, -1, 1, 0.1f, 100);
            }
            if(index == 4)
                submission.view_project_matrix->view =
                    Math::look_at({0, 0, 0}, {0, 1, 0}, {0, 0, 1});
            if(index == 5) {
                for(size_t offset = 0; offset < data.pixels.size(); offset += 8) {
                    const auto packed = glm::packHalf(glm::vec4(2, 0, 0, 1));
                    std::memcpy(data.pixels.data() + offset, &packed, sizeof(packed));
                }
                auto replacement = engine->get_render_resources().try_create_texture(data);
                ASSERT_TRUE(replacement);
                submission.environment_resource = std::make_shared<Environment>(
                    Environment{.background = std::move(replacement).value()});
            }
            if(index == 6)
                submission.environment.background = false;
            if(index == 7) {
                submission.environment.background = true;
                submission.environment_resource.reset();
            }
            if(index == 8) {
                auto replacement = engine->get_render_resources().try_create_texture(data);
                ASSERT_TRUE(replacement);
                submission.environment_resource = std::make_shared<Environment>(
                    Environment{.background = std::move(replacement).value()});
                submission.view_project_matrix =
                    ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                        Math::ortho(-2, 2, -2, 2, 0.1f, 100)};
                auto material = std::make_shared<Material>("foreground", "unlit_color");
                ASSERT_TRUE(material->set_vector_property("color", {0, 1, 0, 1}));
                submission.render_items.push_back(
                    {.mesh = lit_quad(), .material = {AssetHandle(1243), material}});
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), size.x * size.y * 4);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            output_format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], size);
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra =
            output_format == Format::B8G8R8A8_SRGB || output_format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            for(unsigned channel = 0; channel < 3; ++channel) {
                const auto component = bgra ? 2 - channel : channel;
                if(index != 6 && index != 7)
                    EXPECT_NEAR(std::to_integer<int>(bytes[(4 * size.x + 4) * 4 + component]),
                        mapped_byte(expected[index][channel]), 2)
                        << index << ',' << channel;
            }
        }
        EXPECT_EQ(outputs[0]->read(), outputs[1]->read());
        EXPECT_EQ(outputs[6]->read(), outputs[7]->read());
        const auto foreground = outputs[8]->read();
        const unsigned red = bgra ? 2 : 0;
        EXPECT_NEAR(std::to_integer<int>(foreground[red]), mapped_byte(1), 2);
    }

    TEST_F(RenderGraphGpuTest, ImportedHdrSkyboxSurvivesMsaaResolve) {
        Config config;
        config.window.width = 160;
        config.window.height = 120;
        config.vulkan.enable_validation = true;
        config.vulkan.msaa_samples = SampleCount::Count4;
        engine.reset();
        auto created = Engine::create(config);
        ASSERT_TRUE(created);
        engine = std::move(created).value();
        TemporaryDirectory directory;
        write_hdr(directory.path() / "sky.hdr");
        auto imported = EnvironmentImporter{}.import(directory.path() / "sky.hdr");
        ASSERT_TRUE(imported);
        auto texture = Environment::try_create(engine->get_render_resources(), imported.value());
        ASSERT_TRUE(texture);
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({2, 2}));
        RenderSubmission submission{.view_project_matrix = ViewProjectMatrix{Math::Mat4(1),
                                        Math::perspective(60.0f, 1, 0.1f, 100)},
            .environment = {{}, true, 1, 0},
            .environment_resource = texture.value()};
        FrameScheduler frames(device, config.render.max_frames_in_flight);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        frames.get_current_command_buffer().begin();
        auto& scene = renderer.get_scene_renderer();
        auto drawn = scene.render(frames, submission);
        ASSERT_TRUE(drawn) << drawn.error();
        auto output =
            std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
        const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
        copy_output(frames, view->get_image(), output, {2, 2});
        submit(device, frames, drawn.value());
        frames.wait_for_all_slots();
        const auto format = view->get_image()->get_info().format;
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const auto pixels = output->read();
        const Math::Vec3 expected(4, 2, 1);
        for(size_t pixel = 0; pixel < 4; ++pixel)
            for(unsigned channel = 0; channel < 3; ++channel) {
                const unsigned component = bgra ? 2 - channel : channel;
                EXPECT_NEAR(std::to_integer<int>(pixels[pixel * 4 + component]),
                    mapped_byte(expected[channel]), 2);
            }
    }

    TEST_F(RenderGraphGpuTest, MaterialEditCandidatesPublishAtomicallyAndKeepOldFramePixels) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({2, 2}));
        auto& scene = renderer.get_scene_renderer();
        const AssetHandle handle(786);
        auto material = std::make_shared<Material>("authored", "unlit_color");
        ASSERT_TRUE(material->set_vector_property("color", {0, 1, 0, 1}));
        auto pbr = std::make_shared<Material>("authored", "pbr");
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {handle, material}}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        Format format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                // 丢弃有效 GPU 候选，模拟后续保存文件失败而未发布。
                auto abandoned = renderer.prepare_material_update(handle, pbr);
                ASSERT_TRUE(abandoned) << abandoned.error();
                EXPECT_FALSE(renderer.prepare_material_update(
                    handle, std::make_shared<Material>("invalid", "missing_template")));
            } else if(index == 2) {
                auto update = renderer.prepare_material_update(handle, pbr);
                ASSERT_TRUE(update) << update.error();
                std::move(update).value().publish();
                submission.render_items.front().material.resource = pbr;
            } else if(index == 3) {
                auto red = std::make_shared<Material>("authored", "unlit_color");
                ASSERT_TRUE(red->set_vector_property("color", {1, 0, 0, 1}));
                auto update = renderer.prepare_material_update(handle, red);
                ASSERT_TRUE(update) << update.error();
                std::move(update).value().publish();
                submission.render_items.front().material.resource = std::move(red);
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1);
            if(index > 0)
                EXPECT_EQ(scene.get_material_statistics().material_bindings_created, 0);
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], {2, 2});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
        const std::array<Math::Vec3, 4> expected{
            Math::Vec3(0, 1, 0), Math::Vec3(0, 1, 0), Math::Vec3(0), Math::Vec3(1, 0, 0)};
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            for(size_t pixel = 0; pixel < 4; ++pixel)
                for(unsigned channel = 0; channel < 3; ++channel) {
                    const auto component = bgra ? 2 - channel : channel;
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + component]),
                        mapped_byte(expected[index][channel]), 2)
                        << index;
                }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrTextureUsesUvColorSpaceAndFallsBackAfterClear) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({2, 2}));
        auto& scene = renderer.get_scene_renderer();
        auto material = std::make_shared<Material>("pbr", "pbr");
        const Math::Vec3 tint{0.8f, 0.4f, 0.2f};
        ASSERT_TRUE(material->set_vector_property("base_color", Math::Vec4(tint, 1)));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(785), material}}},
            .lights = {{.intensity = Math::PI}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        const std::vector<uint8_t> pixels{
            128, 64, 32, 0, 255, 0, 128, 255, 0, 255, 64, 255, 255, 255, 255, 255};
        std::array<std::shared_ptr<Readback>, 4> outputs;
        Format output_format{};
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1 || index == 2) {
                auto format = Format::R8G8B8A8_SRGB;
                if(index == 2)
                    format = Format::R8G8B8A8_UNORM;
                auto texture = engine->get_render_resources().try_create_texture(
                    {.width = 2, .height = 2, .format = format, .pixels = pixels});
                ASSERT_TRUE(texture) << texture.error();
                material->set_texture_property("base_color_texture", std::move(texture).value());
            } else {
                material->set_texture_property("base_color_texture", nullptr);
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(scene.get_material_statistics().draw_calls, 1);
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
            const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            output_format = view->get_image()->get_info().format;
            copy_output(frames, view->get_image(), outputs[index], {2, 2});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        const bool bgra =
            output_format == Format::B8G8R8A8_SRGB || output_format == Format::B8G8R8A8_UNORM;
        for(size_t index = 0; index < outputs.size(); ++index) {
            SCOPED_TRACE(index);
            const auto bytes = outputs[index]->read();
            for(size_t pixel = 0; pixel < 4; ++pixel) {
                // 场景使用负高度 viewport，读回行序与此测试网格的 V 方向相反。
                const size_t texel_index = (1 - pixel / 2) * 2 + pixel % 2;
                for(unsigned channel = 0; channel < 3; ++channel) {
                    float texel = 1;
                    if(index == 1 || index == 2)
                        texel = pixels[texel_index * 4 + channel] / 255.0f;
                    if(index == 1) {
                        if(texel <= 0.04045f)
                            texel /= 12.92f;
                        else
                            texel = std::pow((texel + 0.055f) / 1.055f, 2.4f);
                    }
                    const auto component = bgra ? 2 - channel : channel;
                    // 正面、roughness=1、非金属、辐照度 PI：漫反射 0.96*base，镜面 0.01。
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + component]),
                        mapped_byte(0.96f * tint[channel] * texel + 0.01f), 3);
                }
                EXPECT_EQ(std::to_integer<int>(bytes[pixel * 4 + 3]), 255);
            }
        }
    }

    TEST_F(RenderGraphGpuTest, PbrReloadRetainsOldFramesAndSurvivesTargetRebuild) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
        auto& scene = renderer.get_scene_renderer();
        const auto directory = std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/material";
        auto source = read_text_file(directory / "pbr.frag");
        ASSERT_TRUE(source) << source.error();
        const std::string color = "clamp(result, 0.0, 65504.0)";
        const auto offset = source.value().find(color);
        ASSERT_NE(offset, std::string::npos);
        source.value().replace(offset, color.size(), "clamp(result * 0.5, 0.0, 65504.0)");
        TemporaryDirectory temporary;
        const auto path = temporary.path() / "pbr.frag";
        ASSERT_TRUE(write_text_file_atomic(path, source.value()));
        const auto compiled = ShaderCompiler::compile(
            {.source = path, .stage = ShaderStage::Fragment, .include_directories = {directory}});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        const MaterialShaders candidate{
            {"pbr", {{PBR_VERT.begin(), PBR_VERT.end()}, compiled.words}}};
        auto material = std::make_shared<Material>("pbr", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.5f, 0.5f, 0.5f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = lit_quad(), .material = {AssetHandle(784), material}}},
            .lights = {{.intensity = Math::PI}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::shared_ptr<Readback>, 3> outputs;
        for(size_t index = 0; index < outputs.size(); ++index) {
            if(index == 1) {
                const auto published = renderer.reload_material_shaders(candidate);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().pipelines, 1);
                EXPECT_EQ(published.value().material_bindings, 0);
                auto broken = candidate;
                broken.at("pbr").fragment.clear();
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
                broken.at("pbr").fragment = {0};
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
                auto header = read_text_file(directory.parent_path() / "lighting/forward.glsl");
                ASSERT_TRUE(header) << header.error();
                const auto binding = header.value().find("binding = 1");
                ASSERT_NE(binding, std::string::npos);
                header.value().replace(
                    binding, std::string_view("binding = 1").size(), "binding = 7");
                auto incompatible_source = source.value();
                const std::string include = "#include \"../lighting/forward.glsl\"";
                const auto offset = incompatible_source.find(include);
                ASSERT_NE(offset, std::string::npos);
                incompatible_source.replace(offset, include.size(), header.value());
                const auto incompatible_path = temporary.path() / "incompatible.frag";
                ASSERT_TRUE(write_text_file_atomic(incompatible_path, incompatible_source));
                const auto incompatible = ShaderCompiler::compile({.source = incompatible_path,
                    .stage = ShaderStage::Fragment,
                    .include_directories = {directory}});
                ASSERT_TRUE(incompatible.succeeded()) << incompatible.diagnostics;
                broken.at("pbr").fragment = incompatible.words;
                EXPECT_FALSE(renderer.reload_material_shaders(broken));
            }
            if(index == 2) {
                // 更新其他程序及重建目标不能恢复旧的 PBR 字节码。
                const auto builtin = default_material_shaders();
                ASSERT_TRUE(
                    renderer.reload_material_shaders({{"unlit_color", builtin.at("unlit_color")}}));
                ASSERT_TRUE(renderer.enable_offscreen_rendering({4, 4}));
            }
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            outputs[index] =
                std::make_shared<Readback>(device, context.get_context().get_physical_device(), 64);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())->get_image(),
                outputs[index], {4, 4});
            submit(device, frames, drawn.value());
        }
        frames.wait_for_all_slots();
        for(size_t index = 0; index < outputs.size(); ++index) {
            const auto bytes = outputs[index]->read();
            const auto expected = mapped_byte(index == 0 ? 0.49f : 0.245f);
            for(size_t pixel = 0; pixel < 16; ++pixel)
                for(size_t channel = 0; channel < 3; ++channel)
                    EXPECT_NEAR(std::to_integer<int>(bytes[pixel * 4 + channel]), expected, 2);
        }
    }

    TEST_F(RenderGraphGpuTest, PbrReceivesDirectionalShadowWithoutMaterialRebinding) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({33, 33}));
        auto& scene = renderer.get_scene_renderer();
        const auto mesh = lit_quad();
        auto material = std::make_shared<Material>("pbr", "pbr");
        const auto occluder =
            Math::scale(Math::translate(Math::Mat4(1), {-0.5f, 0, 0.875f}), {0.25f, 0.25f, 0.25f});
        RenderSubmission submission{
            .view_project_matrix = ViewProjectMatrix{Math::look_at({0, 0, 3}, {0, 0, 0}, {0, 1, 0}),
                Math::ortho(-1, 1, -1, 1, 0.1f, 10)},
            .render_items = {{.mesh = mesh, .material = {AssetHandle(780), material}},
                {.model_matrix = occluder, .mesh = mesh, .material = {AssetHandle(780), material}}},
            .lights = {{.direction = {1, 0, -1}, .intensity = 4}}};
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        std::array<std::vector<std::byte>, 2> pixels;
        for(unsigned index = 0; index < 2; ++index) {
            submission.lights[0].casts_shadow = index == 1;
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto drawn = scene.render(frames, submission);
            ASSERT_TRUE(drawn) << drawn.error();
            EXPECT_EQ(
                scene.get_material_statistics().material_bindings_created, index == 0 ? 1 : 0);
            auto output = std::make_shared<Readback>(
                device, context.get_context().get_physical_device(), 33 * 33 * 4);
            copy_output(frames,
                scene.get_offscreen_color_view(frames.get_current_frame_slot_index())->get_image(),
                output, {33, 33});
            submit(device, frames, drawn.value());
            frames.wait_for_all_slots();
            pixels[index] = output->read();
        }
        for(unsigned channel = 0; channel < 3; ++channel) {
            const unsigned center = (16 * 33 + 16) * 4 + channel;
            const unsigned lit = (16 * 33 + 28) * 4 + channel;
            EXPECT_GT(std::to_integer<int>(pixels[0][center]), 50);
            EXPECT_LE(std::to_integer<int>(pixels[1][center]), 3);
            EXPECT_NEAR(
                std::to_integer<int>(pixels[0][lit]), std::to_integer<int>(pixels[1][lit]), 2);
        }
    }

    TEST_F(RenderGraphGpuTest, ForwardLightsProduceExpectedPixelsAndReuseMaterialBindings) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto& renderer = engine->get_renderer();
        ASSERT_TRUE(renderer.enable_offscreen_rendering({17, 17}));
        auto& scene = renderer.get_scene_renderer();
        auto mesh = lit_quad();
        auto tilted = lit_quad({1, 0, 1});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(tilted);
        auto material = std::make_shared<Material>("lit", "pbr");
        ASSERT_TRUE(material->set_vector_property("base_color", {0.5f, 0.25f, 0.125f, 1}));
        ASSERT_TRUE(material->set_scalar_property("roughness", 1));
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        auto readback = std::make_shared<Readback>(
            device, context.get_context().get_physical_device(), 17 * 17 * 4);
        enum class Scenario {
            Directional,
            Point,
            Spot,
            BackFacing,
            NonuniformScale,
            SingularScale,
            NarrowSpotCone,
            NoLights,
            OutOfRange
        };
        for(const auto& [scenario, name] : {std::pair{Scenario::Directional, "directional light"},
                std::pair{Scenario::Point, "point light"}, std::pair{Scenario::Spot, "spot light"},
                std::pair{Scenario::BackFacing, "back-facing light"},
                std::pair{Scenario::NonuniformScale, "nonuniform normal scale"},
                std::pair{Scenario::SingularScale, "singular model scale"},
                std::pair{Scenario::NarrowSpotCone, "narrow spot cone"},
                std::pair{Scenario::NoLights, "no lights"},
                std::pair{Scenario::OutOfRange, "point light out of range"}}) {
            SCOPED_TRACE(name);
            RenderLight light{.entity_id = 1,
                .position = {0, 0, 2.5f},
                .intensity = Math::PI,
                .range = 10,
                .inner_angle = 5,
                .outer_angle = 20};
            const bool spot = scenario == Scenario::Spot || scenario == Scenario::NarrowSpotCone;
            const bool attenuated = scenario == Scenario::Point || spot;
            if(attenuated) {
                light.type = spot ? LightType::Spot : LightType::Point;
                light.intensity = 4 * Math::PI;
            }
            if(scenario == Scenario::NarrowSpotCone) {
                light.inner_angle = 0;
                light.outer_angle = 0.0001f;
            }
            if(scenario == Scenario::BackFacing)
                light.direction = {0, 0, 1};
            if(scenario == Scenario::OutOfRange) {
                light.type = LightType::Point;
                light.range = 0.5f;
            }
            auto model = Math::Mat4(1);
            if(scenario == Scenario::NonuniformScale)
                model = Math::scale(model, {2, 1, 1});
            if(scenario == Scenario::SingularScale)
                model = Math::scale(model, {1, 1, 0});
            RenderSubmission submission{
                .view_project_matrix = ViewProjectMatrix{Math::Mat4(1), Math::Mat4(1)},
                .render_items = {{.model_matrix = model,
                    .mesh = scenario == Scenario::NonuniformScale ? tilted : mesh,
                    .material = {AssetHandle(555), material}}},
                .lights = {light}};
            if(scenario == Scenario::NoLights)
                submission.lights.clear();
            frames.wait_for_current_slot();
            frames.begin_frame(0);
            frames.get_current_command_buffer().begin();
            const auto rendered = scene.render(frames, submission, {});
            ASSERT_TRUE(rendered) << rendered.error().message;
            const auto& statistics = scene.get_material_statistics();
            EXPECT_EQ(statistics.draw_calls, 1);
            EXPECT_EQ(statistics.light_count, scenario == Scenario::NoLights ? 0 : 1);
            if(scenario != Scenario::Directional)
                EXPECT_EQ(statistics.material_bindings_created, 0);
            auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
            copy_output(frames, view->get_image(), readback, {17, 17});
            submit(device, frames, rendered.value());
            frames.wait_for_all_slots();
            const auto bytes = readback->read();
            const auto format = view->get_image()->get_info().format;
            const bool bgra = format == Format::B8G8R8A8_SRGB || format == Format::B8G8R8A8_UNORM;
            for(unsigned y = 0; y < 17; ++y) {
                for(unsigned x = 0; x < 17; ++x) {
                    double radiance = light.intensity;
                    glm::dvec3 direction = -glm::dvec3(light.direction);
                    if(attenuated) {
                        const Math::Vec3 position{
                            (x + 0.5f) / 8.5f - 1, 1 - (y + 0.5f) / 8.5f, 0.5f};
                        const auto delta = light.position - position;
                        const float distance = Math::length(delta);
                        const float falloff =
                            std::max(1.0f - std::pow(distance / light.range, 4.0f), 0.0f);
                        radiance *= falloff * falloff / (distance * distance);
                        direction = glm::dvec3(delta / distance);
                        if(spot) {
                            const float inner = std::cos(Math::radians(light.inner_angle));
                            const float outer = std::cos(Math::radians(light.outer_angle));
                            if(inner - outer > 1e-6f) {
                                const float t = std::clamp(
                                    (delta.z / distance - outer) / (inner - outer), 0.0f, 1.0f);
                                radiance *= t * t * (3 - 2 * t);
                            } else if(delta.z / distance < outer) {
                                radiance = 0;
                            }
                        }
                    }
                    if(scenario == Scenario::BackFacing || scenario == Scenario::SingularScale
                        || scenario == Scenario::NoLights || scenario == Scenario::OutOfRange)
                        radiance = 0;
                    glm::dvec3 normal{0, 0, 1};
                    if(scenario == Scenario::NonuniformScale)
                        normal = {0.5, 0, 1};
                    const auto expected = pbr_reference(
                        normal, {0, 0, 1}, direction, {0.5, 0.25, 0.125}, 0, 1, radiance);
                    for(size_t channel = 0; channel < 3; ++channel) {
                        const auto component = bgra ? 2 - channel : channel;
                        EXPECT_NEAR(std::to_integer<int>(bytes[(y * 17 + x) * 4 + component]),
                            mapped_byte(static_cast<float>(expected[channel])), 3)
                            << "at " << x << ',' << y;
                    }
                }
            }
        }
    }

}
