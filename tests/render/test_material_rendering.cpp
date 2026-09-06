#include "core/engine.h"
#include "diagnostics/logger.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/convert.h"
#include "render/material.h"
#include "render/material_renderer.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "shader_reload.h"
#include "common/file_io.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <sstream>
#include <tuple>

namespace Comet::Tests {
    class MaterialRenderingTest: public ::testing::Test {
    protected:
        void SetUp() override {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            Logger::add_custom_sink(sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            engine = std::make_unique<Engine>(config);
        }
        void TearDown() override {
            engine->get_renderer().get_render_context().wait_idle();
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos)
                << messages.str();
            std::error_code error;
            std::filesystem::remove_all(shader_root, error);
        }
        void verify_versions(bool reload_shaders, bool change_layout = false);
        std::shared_ptr<Texture> texture(std::vector<uint8_t> rgba) {
            return engine->get_resource_manager()
                .try_create_texture({.width = 1, .height = 1, .pixels = std::move(rgba)})
                .value();
        }
        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
        std::filesystem::path shader_root =
            std::filesystem::temp_directory_path()
            / ("comet_material_shader_test_"
                + std::to_string(AssetHandle::generate().value()));
    };

    void MaterialRenderingTest::verify_versions(
        const bool reload_shaders, const bool change_layout) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::TransferSrcOptimal;
        color.usage |= ImageUsage::CopySrc;
        RenderPass pass(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT)},
            {RenderSubPass{
                {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
            Format::R8G8B8A8_UNORM);
        auto target = RenderTarget::create_multi_target(device, pass, {64, 32}, 2);
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        MaterialRenderer materials(
            device, pipelines, engine->get_resource_manager(), 2, SampleCount::Count1);
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        const MeshData mesh_data{
            .vertices = {{{-0.4f, -0.8f, 0.5f}}, {{0.4f, -0.8f, 0.5f}},
                {{0.4f, 0.8f, 0.5f}}, {{-0.4f, 0.8f, 0.5f}}},
            .indices = {0, 1, 2, 2, 3, 0}};
        const auto mesh =
            engine->get_resource_manager().try_create_mesh(mesh_data).value();
        const auto textured = std::make_shared<Material>("textured", "cube_texture");
        textured->set_texture_property("u_Texture0", texture({255, 0, 0, 255}));
        const std::weak_ptr<Texture> retired =
            textured->get_texture_property("u_Texture0");
        textured->set_texture_property("u_Texture1", texture({0, 0, 255, 255}));
        textured->set_scalar_property("blend", 0.25f);
        textured->set_vector_property("tint", {1, 0.5f, 0.5f, 1});
        const auto solid = std::make_shared<Material>("solid", "unlit_color");
        solid->set_vector_property("color", {0.2f, 0.8f, 0.4f, 1});
        solid->set_scalar_property("intensity", 0.5f);
        const std::vector<ResolvedRenderItem> items{
            {.model_matrix = Math::translate(Math::Mat4(1), {-0.5f, 0, 0}),
                .mesh = mesh,
                .material = {AssetHandle(1), textured}},
            {.model_matrix = Math::translate(Math::Mat4(1), {0.5f, 0, 0}),
                .mesh = mesh,
                .material = {AssetHandle(2), solid}}};

        // 测试专用 host-coherent readback，不扩大生产 Texture 的用途或接口。
        vk::UniqueDeviceMemory memory;
        auto readback =
            device.get().createBufferUnique(vk::BufferCreateInfo({}, 2 * 64 * 32 * 4,
                vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));
        const auto requirements = device.get().getBufferMemoryRequirements(*readback);
        const auto properties =
            context.get_context().get_physical_device().getMemoryProperties();
        std::optional<uint32_t> memory_type;
        const auto required = vk::MemoryPropertyFlagBits::eHostVisible
                              | vk::MemoryPropertyFlagBits::eHostCoherent;
        for(uint32_t index = 0; index < properties.memoryTypeCount; ++index) {
            if((requirements.memoryTypeBits & (1u << index))
                && (properties.memoryTypes[index].propertyFlags & required) == required) {
                memory_type = index;
                break;
            }
        }
        ASSERT_TRUE(memory_type);
        memory = device.get().allocateMemoryUnique(
            vk::MemoryAllocateInfo(requirements.size, *memory_type));
        device.get().bindBufferMemory(*readback, *memory, 0);
        context.wait_idle();
        engine->get_resource_manager().collect_completed_uploads();
        for(int iteration = 0; iteration < 2; ++iteration) {
            if(iteration == 1) {
                if(reload_shaders) {
                    CometEditor::ShaderReload::Requests requests;
                    for(const auto& [name, filename, stage] :
                        {std::tuple("material_mesh", "material_mesh.vert",
                             ShaderCompiler::Stage::Vertex),
                            std::tuple("material_textured", "material_textured.frag",
                                ShaderCompiler::Stage::Fragment),
                            std::tuple("material_solid", "material_solid.frag",
                                ShaderCompiler::Stage::Fragment)}) {
                        auto source =
                            read_text_file(std::filesystem::path(PROJECT_ROOT_DIR)
                                           / "engine/shaders/glsl" / filename);
                        if(stage == ShaderCompiler::Stage::Fragment)
                            source.insert(
                                source.rfind('}'), "    color.rgb = color.bgr;\n");
                        if(change_layout && stage == ShaderCompiler::Stage::Fragment) {
                            const auto replace = [&](const std::string& from,
                                                     const std::string& to) {
                                const auto offset = source.find(from);
                                ASSERT_NE(offset, std::string::npos);
                                source.replace(offset, from.size(), to);
                            };
                            replace("binding = 0, std140", "binding = 5, std140");
                            if(std::string_view(name) == "material_textured") {
                                replace("vec4 tint;\n    float blend;",
                                    "layout(offset=0) float blend;\n    layout(offset=32) vec4 tint;");
                                replace("binding = 1)", "binding = 4)");
                                replace("binding = 2)", "binding = 0)");
                            } else {
                                replace("vec4 color;\n    float intensity;",
                                    "layout(offset=0) float intensity;\n    layout(offset=32) vec4 color;");
                            }
                        }
                        write_text_file_atomic(shader_root / filename, source);
                        requests.emplace(
                            name, ShaderCompiler::Request{
                                      .source = shader_root / filename, .stage = stage});
                    }
                    CometEditor::ShaderReload reload(
                        engine->get_task_scheduler(), requests);
                    EXPECT_FALSE(reload.update());
                    engine->get_task_scheduler().wait_idle();
                    const auto candidate = reload.update();
                    ASSERT_TRUE(candidate);
                    auto& shader_manager =
                        engine->get_resource_manager().get_shader_manager();
                    if(change_layout) {
                        const auto old_layouts = materials.get_material_layouts();
                        const auto old_shader =
                            shader_manager.get_shader("material_solid");
                        const auto old_texture =
                            textured->get_texture_property("u_Texture0");
                        textured->set_texture_property("u_Texture0", nullptr);
                        EXPECT_THROW(materials.reload_shaders(pipelines, shader_manager,
                                         *candidate, SampleCount::Count1),
                            std::runtime_error);
                        EXPECT_EQ(
                            shader_manager.get_shader("material_solid"), old_shader);
                        EXPECT_EQ(materials.get_material_layouts(), old_layouts);
                        textured->set_texture_property("u_Texture0", old_texture);
                    }
                    const auto report = materials.reload_shaders(
                        pipelines, shader_manager, *candidate, SampleCount::Count1);
                    EXPECT_EQ(report.pipelines, 2u);
                    EXPECT_EQ(report.material_versions, 2u);
                    EXPECT_EQ(report.material_bindings, change_layout ? 2u : 0u);
                    if(change_layout) {
                        for(const auto& layout : materials.get_material_layouts()) {
                            EXPECT_EQ(layout->get_parameter_size(), 48u);
                            EXPECT_EQ(layout->get_parameter_binding(), 5u);
                            EXPECT_EQ(layout->get_scalars().front().offset, 0u);
                            EXPECT_EQ(layout->get_vectors().front().offset, 32u);
                        }
                    }
                    EXPECT_EQ(shader_manager.get_shader("material_solid")->get_code(),
                        candidate->at("material_solid").words);
                    auto broken = *candidate;
                    auto changed_vertex =
                        read_text_file(shader_root / "material_mesh.vert");
                    changed_vertex.insert(
                        changed_vertex.rfind('}'), "    gl_Position.x += 0.0;\n");
                    write_text_file_atomic(
                        shader_root / "material_mesh.vert", changed_vertex);
                    const auto compiled_vertex =
                        ShaderCompiler::compile(requests.at("material_mesh"));
                    ASSERT_TRUE(compiled_vertex.succeeded())
                        << compiled_vertex.diagnostics;
                    broken.at("material_mesh").words = compiled_vertex.words;
                    broken.at("material_solid").words.clear();
                    const auto old_vertex = shader_manager.get_shader("material_mesh");
                    EXPECT_THROW(materials.reload_shaders(pipelines, shader_manager,
                                     broken, SampleCount::Count1),
                        std::invalid_argument);
                    EXPECT_EQ(shader_manager.get_shader("material_solid")->get_code(),
                        candidate->at("material_solid").words);
                    EXPECT_EQ(shader_manager.get_shader("material_mesh"), old_vertex);
                    // 重建使用已发布的 Shader，不重新覆盖成嵌入的初始版本。
                    MaterialRenderer rebuilt(device, pipelines,
                        engine->get_resource_manager(), 2, SampleCount::Count1);
                    if(change_layout) {
                        for(const auto& layout : rebuilt.get_material_layouts()) {
                            EXPECT_EQ(layout->get_parameter_size(), 48u);
                            EXPECT_EQ(layout->get_parameter_binding(), 5u);
                        }
                    }
                    EXPECT_EQ(shader_manager.get_shader("material_solid")->get_code(),
                        candidate->at("material_solid").words);
                } else {
                    textured->set_texture_property(
                        "u_Texture0", texture({255, 0, 0, 255}));
                    textured->set_scalar_property("blend", 0.75f);
                    solid->set_scalar_property("intensity", 0.25f);
                }
            }
            frames.wait_for_current_slot();
            const auto slot = frames.get_current_frame_slot_index();
            frames.begin_frame(slot);
            auto& command = frames.get_current_command_buffer();
            command.begin(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            target->begin_render_target(command, slot);
            command.set_viewport(Graphics::get_viewport(64, 32));
            command.set_scissor(Graphics::get_scissor(64, 32));
            const auto waits = materials.render(
                frames, {.view = Math::Mat4(1), .projection = Math::Mat4(1)}, items);
            target->end_render_target(command);
            vk::MemoryBarrier barrier(vk::AccessFlagBits::eColorAttachmentWrite,
                vk::AccessFlagBits::eTransferRead);
            command.get().pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
            vk::BufferImageCopy copy;
            copy.bufferOffset = iteration * 64 * 32 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(64, 32, 1);
            command.get().copyImageToBuffer(
                target->get_color_view(slot)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
            barrier = vk::MemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            static_cast<void>(
                device.get_graphics_queue(0).submit2(waits, std::span(&command, 1), {},
                    &frames.get_current_frame_slot().in_flight_fence));
            frames.record_submission();
            frames.end_frame();
            uint32_t expected_versions = 2;
            uint32_t expected_bindings = 2;
            if(iteration == 1 && reload_shaders) {
                expected_bindings = 0;
                expected_versions = 0;
            }
            EXPECT_EQ(
                materials.get_statistics().material_versions_created, expected_versions);
            EXPECT_EQ(
                materials.get_statistics().material_bindings_created, expected_bindings);
        }
        // 两个独立 slot 已提交，但尚未进行完成回收；旧版本必须仍有 owner。
        EXPECT_FALSE(retired.expired());
        if(reload_shaders) {
            pipelines.collect_unused();
            EXPECT_EQ(pipelines.get_cached_pipeline_count(), 4u);
        }
        frames.wait_for_all_slots();
        if(reload_shaders) {
            pipelines.collect_unused();
            EXPECT_EQ(pipelines.get_cached_pipeline_count(), 2u);
        } else {
            EXPECT_TRUE(retired.expired());
        }
        const auto* all_pixels = static_cast<const uint8_t*>(
            device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        for(int iteration = 0; iteration < 2; ++iteration) {
            const auto* pixels = all_pixels + iteration * 64 * 32 * 4;
            const auto check = [&](uint32_t x, std::array<int, 3> expected) {
                for(std::size_t channel = 0; channel < 3; ++channel) {
                    EXPECT_NEAR(pixels[(16 * 64 + x) * 4 + channel], expected[channel], 2)
                        << "iteration=" << iteration << " x=" << x
                        << " channel=" << channel;
                }
            };
            if(iteration == 0) {
                check(16, {191, 0, 32});
                check(48, {26, 102, 51});
            } else if(reload_shaders) {
                check(16, {32, 0, 191});
                check(48, {51, 102, 26});
            } else {
                check(16, {64, 0, 96});
                check(48, {13, 51, 26});
            }
        }
        device.get().unmapMemory(*memory);
    }

    TEST_F(
        MaterialRenderingTest, ReadsPixelsFromTwoLayoutsBeforeAndAfterParameterChanges) {
        verify_versions(false);
    }

    TEST_F(MaterialRenderingTest,
        ReloadsShadersWithoutChangingMaterialOrRetiringInFlightPipelines) {
        verify_versions(true);
    }

    TEST_F(
        MaterialRenderingTest, RebuildsAllResidentBindingsAtomicallyAndRetainsOldFrames) {
        verify_versions(true, true);
    }

    TEST_F(MaterialRenderingTest, SceneRendererOnlyPublishesBetweenFrames) {
        auto& renderer = engine->get_renderer();
        auto& scene = renderer.get_scene_renderer();
        auto& frames = scene.get_frame_scheduler();
        auto& resources = renderer.get_resource_manager();
        ShaderManager::Bytecodes unchanged;
        for(const auto* name : {"material_mesh", "material_textured", "material_solid"}) {
            const auto shader = resources.get_shader_manager().get_shader(name);
            unchanged.emplace(name, ShaderManager::Bytecode{shader->get_code(),
                                        shader->get_interface().get_entry_point()});
        }
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        EXPECT_THROW(
            scene.reload_material_shaders(resources, unchanged), std::logic_error);
        auto& command = frames.get_current_command_buffer();
        command.begin();
        command.end();
        static_cast<void>(
            renderer.get_render_context().get_device().get_graphics_queue().submit2({},
                std::span(&command, 1), {},
                &frames.get_current_frame_slot().in_flight_fence));
        frames.record_submission();
        frames.end_frame();
        EXPECT_NO_THROW(scene.reload_material_shaders(resources, unchanged));
        frames.wait_for_all_slots();
        const auto report = scene.reload_material_shaders(resources, unchanged);
        EXPECT_EQ(report.pipelines, 0u);
        EXPECT_EQ(report.material_versions, 0u);
        const auto old_shader =
            resources.get_shader_manager().get_shader("material_mesh");
        auto source = read_text_file(std::filesystem::path(PROJECT_ROOT_DIR)
                                     / "engine/shaders/glsl/material_mesh.vert");
        const std::string original_fields = "mat4 view;\n    mat4 projection;";
        const auto offset = source.find(original_fields);
        ASSERT_NE(offset, std::string::npos);
        source.replace(
            offset, original_fields.size(), "mat4 projection;\n    mat4 view;");
        write_text_file_atomic(shader_root / "fixed.vert", source);
        const auto compiled =
            ShaderCompiler::compile({.source = shader_root / "fixed.vert",
                .stage = ShaderCompiler::Stage::Vertex});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        unchanged.at("material_mesh").words = compiled.words;
        EXPECT_THROW(
            scene.reload_material_shaders(resources, unchanged), std::invalid_argument);
        EXPECT_EQ(resources.get_shader_manager().get_shader("material_mesh"), old_shader);
    }
}
