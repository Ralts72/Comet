#include "support/render_graph_gpu_fixture.h"
#include "asset/artifact/shader_program_artifact.h"
#include "asset/registry.h"
#include "render/material/material_layout.h"
#include "render/material/material_programs.h"
#include "unlit_color_vert.h"
#include "pbr_frag.h"
#include "scene/material_parameters.h"

#include <algorithm>

namespace Comet::Tests {
    TEST_F(RenderGraphGpuTest, IblLightsDielectricsAndMetalsWithoutBackgroundAndKeepsOldFrames) {
        auto& renderer = engine->get_renderer();
        auto& context = renderer.get_render_context();
        auto& device = context.get_device();
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {9, 9});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
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
            begin_frame(frames);
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
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {9, 9});
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
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
            begin_frame(frames);
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
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, size);
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
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
            begin_frame(frames);
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
        MaterialPrograms programs(engine->get_asset_registry());
        auto scene_owner = create_scene(programs, {2, 2}, SampleCount::Count4);
        ASSERT_TRUE(scene_owner) << scene_owner.error();
        auto& scene = *scene_owner.value();
        RenderSubmission submission{.view_project_matrix = ViewProjectMatrix{Math::Mat4(1),
                                        Math::perspective(60.0f, 1, 0.1f, 100)},
            .environment = {{}, true, 1, 0},
            .environment_resource = texture.value()};
        FrameScheduler frames(device, config.render.max_frames_in_flight);
        frames.initialize_swapchain_images(2);
        FrameWait wait{device, frames};
        begin_frame(frames);
        auto drawn = scene.render(frames, submission);
        ASSERT_TRUE(drawn) << drawn.error();
        auto output =
            std::make_shared<Readback>(device, context.get_context().get_physical_device(), 16);
        const auto view = scene.get_offscreen_color_view(frames.get_current_frame_slot_index());
        ASSERT_NO_FATAL_FAILURE(finish_readback(scene, frames, output, {2, 2}, drawn.value()));
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
}
