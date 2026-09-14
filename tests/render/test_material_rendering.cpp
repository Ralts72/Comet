#include "core/engine.h"
#include "core/math_utils.h"
#include "support/engine_fixture.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "render/frame_scheduler.h"
#include "render/resource/resource_manager.h"
#include "render/resource/mesh_data.h"
#include "render/resource/texture_data.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/attachment.h"
#include "graphics/render_pass.h"
#include "graphics/pipeline/pipeline.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/convert.h"
#include "render/material.h"
#include "render/material_renderer.h"
#include "render/debug/debug_renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "shader/compiler.h"
#include "common/file_io.h"
#include "support/temporary_directory.h"
#include "material_mesh_vert.h"
#include "material_textured_frag.h"
#include "material_solid_frag.h"

#include <gtest/gtest.h>
#include <array>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace Comet::Tests {
    static_assert(!std::is_constructible_v<MaterialRenderer, Device&, PipelineManager&,
        ResourceManager&, uint32_t, SampleCount>);
    static_assert(!std::is_constructible_v<DebugRenderer, Device&, PipelineManager&,
        ResourceManager&, uint32_t, SampleCount>);

    class MaterialRenderingTest: public EngineTest {
    protected:
        GpuResourceResult<std::shared_ptr<Texture>> texture(std::vector<uint8_t> rgba) {
            return engine->get_resource_manager().try_create_texture(
                {.width = 1, .height = 1, .pixels = std::move(rgba)});
        }
    };

    TEST_F(MaterialRenderingTest, RejectsMissingFrameSlotsAndCanCreateAfterFailure) {
        auto& device = engine->get_renderer().get_render_context().get_device();
        auto& resources = engine->get_resource_manager();
        const auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        auto pass_result = RenderPass::create(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
            Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        PipelineManager pipelines(device, pass);
        auto materials =
            MaterialRenderer::create(device, pipelines, resources, 0, SampleCount::Count1);
        ASSERT_FALSE(materials);
        EXPECT_FALSE(materials.error().result.has_value());
        auto debug = DebugRenderer::create(device, pipelines, 0, SampleCount::Count1);
        ASSERT_FALSE(debug);
        EXPECT_FALSE(debug.error().result.has_value());
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);

        materials = MaterialRenderer::create(device, pipelines, resources, 2, SampleCount::Count1);
        ASSERT_TRUE(materials) << materials.error();
        debug = DebugRenderer::create(device, pipelines, 2, SampleCount::Count1);
        ASSERT_TRUE(debug) << debug.error();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 3u);
        MaterialRenderer::ShaderCode invalid{
            {std::begin(MATERIAL_MESH_VERT), std::end(MATERIAL_MESH_VERT)},
            {std::begin(MATERIAL_TEXTURED_FRAG), std::end(MATERIAL_TEXTURED_FRAG)}, {0}};
        EXPECT_FALSE(materials.value()->reload_shaders(pipelines, invalid, SampleCount::Count1));
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 3u);
        materials.value().reset();
        debug.value().reset();
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
    }

    TEST_F(MaterialRenderingTest, InvalidOffscreenExtentKeepsCurrentTarget) {
        auto& renderer = engine->get_renderer();
        auto* previous = &renderer.get_scene_renderer().get_render_target();
        const auto size = previous->get_size();
        auto result = renderer.enable_offscreen_rendering({0, 32});
        ASSERT_FALSE(result);
        EXPECT_FALSE(result.error().result.has_value());
        EXPECT_EQ(&renderer.get_scene_renderer().get_render_target(), previous);
        EXPECT_EQ(previous->get_size(), size);
        ASSERT_TRUE(renderer.prepare_frame());
        renderer.render_frame({});
    }

    TEST_F(MaterialRenderingTest, ShaderPublicationRejectsFixedContractChangesAndActiveFrames) {
        auto& renderer = engine->get_renderer();
        auto& scene_renderer = renderer.get_scene_renderer();
        const MaterialRenderer::ShaderCode original{
            {std::begin(MATERIAL_MESH_VERT), std::end(MATERIAL_MESH_VERT)},
            {std::begin(MATERIAL_TEXTURED_FRAG), std::end(MATERIAL_TEXTURED_FRAG)},
            {std::begin(MATERIAL_SOLID_FRAG), std::end(MATERIAL_SOLID_FRAG)}};
        const auto source = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl/material_mesh.vert");
        ASSERT_TRUE(source) << source.error();
        TemporaryDirectory directory;
        for(const auto& [from, to] :
            {std::pair{"mat4 view;\n    mat4 projection;", "mat4 projection;\n    mat4 view;"},
                {"binding = 0, std140", "binding = 0, std140, row_major"},
                {"layout(push_constant)", "layout(push_constant, row_major)"}}) {
            SCOPED_TRACE(to);
            auto modified = source.value();
            const auto offset = modified.find(from);
            ASSERT_NE(offset, std::string::npos);
            modified.replace(offset, std::string_view(from).size(), to);
            const auto path = directory.path() / "modified.vert";
            ASSERT_TRUE(write_text_file_atomic(path, modified));
            const auto compiled = ShaderCompiler::compile({.source = path});
            ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
            auto candidate = original;
            candidate.vertex = compiled.words;
            const auto rejected = renderer.reload_material_shaders(candidate);
            ASSERT_FALSE(rejected);
            EXPECT_FALSE(rejected.error().result.has_value());
            EXPECT_NE(rejected.error().message.find("fixed resource layout"), std::string::npos);

            auto& device = renderer.get_render_context().get_device();
            auto pass = RenderPass::create(device,
                {Attachment::get_color_attachment(Format::R8G8B8A8_UNORM),
                    Attachment::get_depth_attachment(Format::D32_SFLOAT)},
                {RenderSubPass{
                    {}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
                Format::R8G8B8A8_UNORM);
            ASSERT_TRUE(pass) << pass.error();
            PipelineManager pipelines(device, *pass.value());
            auto rebuilt = MaterialRenderer::create(device, pipelines,
                engine->get_resource_manager(), 2, SampleCount::Count1, &candidate);
            EXPECT_FALSE(rebuilt);
            EXPECT_EQ(pipelines.get_cached_pipeline_count(), 0u);
        }
        ASSERT_TRUE(renderer.reload_material_shaders(original));
        ASSERT_TRUE(renderer.prepare_frame());
        EXPECT_TRUE(renderer.get_frame_scheduler().is_frame_active());
        const auto rejected = renderer.reload_material_shaders(original);
        EXPECT_FALSE(rejected);
        if(!rejected)
            EXPECT_NE(rejected.error().message.find("frame boundary"), std::string::npos);
        renderer.render_frame({});
        EXPECT_FALSE(renderer.get_frame_scheduler().is_frame_active());
        ASSERT_TRUE(renderer.reload_material_shaders(original));
    }

    TEST_F(MaterialRenderingTest, OverlayRebuildFailurePropagatesToApplicationBoundary) {
        auto& renderer = engine->get_renderer();
        bool released = false;
        renderer.set_swapchain_resource_callbacks([&] { released = true; },
            [&](const SwapchainCompatibility&) {
                EXPECT_TRUE(released);
                return Result<void, GraphicsError>::failure({"overlay rebuild failed"});
            });
        EXPECT_THROW(static_cast<void>(renderer.recreate_swapchain()), std::runtime_error);
        renderer.set_swapchain_resource_callbacks({}, {});
    }

    TEST_F(MaterialRenderingTest, ReadsPixelsFromTwoLayoutsBeforeAndAfterParameterChanges) {
        auto& context = engine->get_renderer().get_render_context();
        auto& device = context.get_device();
        auto color = Attachment::get_color_attachment(Format::R8G8B8A8_UNORM);
        color.description.store_op = AttachmentStoreOp::Store;
        color.description.final_layout = ImageLayout::TransferSrcOptimal;
        color.usage |= ImageUsage::CopySrc;
        auto pass_result = RenderPass::create(device,
            {color, Attachment::get_depth_attachment(Format::D32_SFLOAT)},
            {RenderSubPass{{}, {SubpassColorAttachment(0)}, {SubpassDepthStencilAttachment(1)}}},
            Format::R8G8B8A8_UNORM);
        ASSERT_TRUE(pass_result) << pass_result.error();
        auto& pass = *pass_result.value();
        auto target_result = RenderTarget::try_create_multi_target(device, pass, {64, 32}, 2);
        ASSERT_TRUE(target_result) << target_result.error();
        auto target = std::move(target_result).value();
        target->set_clear_value(ClearValue(Math::Vec4(0, 0, 0, 1)));
        PipelineManager pipelines(device, pass);
        auto material_result = MaterialRenderer::create(
            device, pipelines, engine->get_resource_manager(), 2, SampleCount::Count1);
        ASSERT_TRUE(material_result) << material_result.error();
        auto materials = std::move(material_result).value();
        FrameScheduler frames(device, 2);
        frames.initialize_swapchain_images(2);
        const MeshData mesh_data{.vertices = {{{-0.4f, -0.8f, 0.5f}}, {{0.4f, -0.8f, 0.5f}},
                                     {{0.4f, 0.8f, 0.5f}}, {{-0.4f, 0.8f, 0.5f}}},
            .indices = {0, 1, 2, 2, 3, 0}};
        const auto mesh_result = engine->get_resource_manager().try_create_mesh(mesh_data);
        ASSERT_TRUE(mesh_result) << mesh_result.error();
        const auto mesh = mesh_result.value();
        const auto textured = std::make_shared<Material>("textured", "unlit_texture_blend");
        {
            auto red = texture({255, 0, 0, 255});
            ASSERT_TRUE(red) << red.error();
            textured->set_texture_property("u_Texture0", std::move(red).value());
        }
        const std::weak_ptr<Texture> retired = textured->get_texture_property("u_Texture0");
        auto blue = texture({0, 0, 255, 255});
        ASSERT_TRUE(blue) << blue.error();
        textured->set_texture_property("u_Texture1", std::move(blue).value());
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

        vk::UniqueDeviceMemory memory;
        auto readback = device.get().createBufferUnique(vk::BufferCreateInfo({}, 4 * 64 * 32 * 4,
            vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive));
        const auto requirements = device.get().getBufferMemoryRequirements(*readback);
        const auto properties = context.get_context().get_physical_device().getMemoryProperties();
        std::optional<uint32_t> memory_type;
        const auto required =
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
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
        TemporaryDirectory sources;
        auto solid_source = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl/material_solid.frag");
        ASSERT_TRUE(solid_source) << solid_source.error();
        const auto expression = solid_source.value().find("material.intensity");
        ASSERT_NE(expression, std::string::npos);
        solid_source.value().insert(expression, "0.5 * ");
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "solid.frag", solid_source.value()));
        const auto compiled = ShaderCompiler::compile(
            {.source = sources.path() / "solid.frag", .stage = ShaderStage::Fragment});
        ASSERT_TRUE(compiled.succeeded()) << compiled.diagnostics;
        const MaterialRenderer::ShaderCode updated{
            {std::begin(MATERIAL_MESH_VERT), std::end(MATERIAL_MESH_VERT)},
            {std::begin(MATERIAL_TEXTURED_FRAG), std::end(MATERIAL_TEXTURED_FRAG)}, compiled.words};
        auto textured_source = read_text_file(
            std::filesystem::path(PROJECT_ROOT_DIR) / "engine/shaders/glsl/material_textured.frag");
        ASSERT_TRUE(textured_source) << textured_source.error();
        const auto assignment = textured_source.value().find("color = ");
        ASSERT_NE(assignment, std::string::npos);
        textured_source.value().insert(assignment + std::string_view("color = ").size(), "0.5 * ");
        ASSERT_TRUE(
            write_text_file_atomic(sources.path() / "textured.frag", textured_source.value()));
        const auto changed_textured = ShaderCompiler::compile(
            {.source = sources.path() / "textured.frag", .stage = ShaderStage::Fragment});
        ASSERT_TRUE(changed_textured.succeeded()) << changed_textured.diagnostics;
        auto broken_solid = solid_source.value();
        broken_solid.insert(
            broken_solid.find("void main"), "layout(location=4) in vec4 missing;\n");
        broken_solid.insert(
            broken_solid.find("color = ") + std::string_view("color = ").size(), "missing * ");
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "broken.frag", broken_solid));
        const auto broken = ShaderCompiler::compile(
            {.source = sources.path() / "broken.frag", .stage = ShaderStage::Fragment});
        ASSERT_TRUE(broken.succeeded()) << broken.diagnostics;
        EXPECT_FALSE(materials->reload_shaders(pipelines,
            {updated.vertex, changed_textured.words, broken.words}, SampleCount::Count1));
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "layout-solid.frag",
            "#version 450\nlayout(location=0) out vec4 color;"
            "layout(set=1,binding=5,std140) uniform MaterialData {float intensity;"
            "layout(offset=32) vec4 color;} material;"
            "void main(){color=vec4(material.color.rgb*material.intensity*0.5,material.color.a);}"));
        ASSERT_TRUE(write_text_file_atomic(sources.path() / "layout-textured.frag",
            "#version 450\nlayout(location=0) in vec2 uv;layout(location=0) out vec4 color;"
            "layout(set=1,binding=3,std140) uniform MaterialData {float blend;"
            "layout(offset=32) vec4 tint;} material;"
            "layout(set=1,binding=7) uniform sampler2D texture0;"
            "layout(set=1,binding=6) uniform sampler2D texture1;"
            "void main(){color=material.tint*mix(texture(texture0,uv),texture(texture1,uv),clamp(material.blend,0.0,1.0));}"));
        const auto layout_solid = ShaderCompiler::compile(
            {.source = sources.path() / "layout-solid.frag", .stage = ShaderStage::Fragment});
        const auto layout_textured = ShaderCompiler::compile(
            {.source = sources.path() / "layout-textured.frag", .stage = ShaderStage::Fragment});
        ASSERT_TRUE(layout_solid.succeeded()) << layout_solid.diagnostics;
        ASSERT_TRUE(layout_textured.succeeded()) << layout_textured.diagnostics;
        const MaterialRenderer::ShaderCode relocated{
            updated.vertex, layout_textured.words, layout_solid.words};
        // 第一条候选有效、第二条失败，随后读回的纹理材质仍应使用原 Shader。
        context.wait_idle();
        engine->get_resource_manager().collect_completed_uploads();
        for(int iteration = 0; iteration < 4; ++iteration) {
            if(iteration == 1) {
                auto red = texture({255, 0, 0, 255});
                ASSERT_TRUE(red) << red.error();
                textured->set_texture_property("u_Texture0", std::move(red).value());
                textured->set_scalar_property("blend", 0.75f);
                // 材质不变，仅替换 Shader；旧槽位此时尚未回收。
                const auto published =
                    materials->reload_shaders(pipelines, updated, SampleCount::Count1);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().material_versions, 1u);
                EXPECT_EQ(published.value().material_bindings, 0u);
            }
            if(iteration == 2) {
                const auto previous_layouts = materials->get_material_layouts();
                const auto previous_texture = textured->get_texture_property("u_Texture0");
                textured->set_texture_property("u_Texture0", nullptr);
                const auto rejected =
                    materials->reload_shaders(pipelines, relocated, SampleCount::Count1);
                EXPECT_FALSE(rejected);
                EXPECT_EQ(materials->get_material_layouts(), previous_layouts);
                textured->set_texture_property("u_Texture0", previous_texture);
                const auto published =
                    materials->reload_shaders(pipelines, relocated, SampleCount::Count1);
                ASSERT_TRUE(published) << published.error();
                EXPECT_EQ(published.value().material_versions, 2u);
                EXPECT_EQ(published.value().material_bindings, 2u);
                for(const auto& layout : materials->get_material_layouts()) {
                    EXPECT_EQ(layout->get_parameter_size(), 48u);
                    if(layout->get_name() == "unlit_texture_blend") {
                        ASSERT_EQ(layout->get_textures().size(), 2u);
                        EXPECT_EQ(layout->get_textures()[0].name, "u_Texture0");
                        EXPECT_EQ(layout->get_textures()[0].binding, 7u);
                        EXPECT_EQ(layout->get_textures()[1].name, "u_Texture1");
                        EXPECT_EQ(layout->get_textures()[1].binding, 6u);
                    }
                }
            }
            if(iteration == 3) {
                const auto before = materials->get_material_layouts();
                const auto repeated =
                    materials->reload_shaders(pipelines, relocated, SampleCount::Count1);
                ASSERT_TRUE(repeated) << repeated.error();
                EXPECT_EQ(repeated.value().pipelines, 0u);
                EXPECT_EQ(repeated.value().material_versions, 0u);
                EXPECT_EQ(repeated.value().material_bindings, 0u);
                EXPECT_EQ(materials->get_material_layouts(), before);
            }
            frames.wait_for_current_slot();
            const auto slot = frames.get_current_frame_slot_index();
            frames.begin_frame(slot);
            auto& command = frames.get_current_command_buffer();
            command.begin(Flags<CommandBuffer::Usage>(CommandBuffer::Usage::OneTimeSubmit));
            target->begin_render_target(command, slot);
            command.set_viewport(Graphics::get_viewport(64, 32));
            command.set_scissor(Graphics::get_scissor(64, 32));
            const auto waits = materials->render(frames,
                ViewProjectMatrix{.view = Math::Mat4(1), .projection = Math::Mat4(1)}, items);
            target->end_render_target(command);
            vk::MemoryBarrier barrier(
                vk::AccessFlagBits::eColorAttachmentWrite, vk::AccessFlagBits::eTransferRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                vk::PipelineStageFlagBits::eTransfer, {}, barrier, {}, {});
            vk::BufferImageCopy copy;
            copy.bufferOffset = iteration * 64 * 32 * 4;
            copy.imageSubresource =
                vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
            copy.imageExtent = vk::Extent3D(64, 32, 1);
            command.get().copyImageToBuffer(target->get_color_view(slot)->get_image()->get(),
                vk::ImageLayout::eTransferSrcOptimal, *readback, copy);
            barrier = vk::MemoryBarrier(
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead);
            command.get().pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eHost, {}, barrier, {}, {});
            command.end();
            const auto submission = frames.submit(waits, {});
            ASSERT_TRUE(submission) << submission.error();
            frames.end_frame();
            const std::array<uint32_t, 4> expected_creations{2, 1, 0, 0};
            EXPECT_EQ(materials->get_statistics().material_versions_created,
                expected_creations[iteration]);
            EXPECT_EQ(materials->get_statistics().material_bindings_created,
                expected_creations[iteration]);
            if(iteration == 1) {
                EXPECT_FALSE(retired.expired());
                pipelines.collect_unused();
                EXPECT_EQ(pipelines.get_cached_pipeline_count(), 3u);
            }
            if(iteration == 2) {
                pipelines.collect_unused();
                EXPECT_EQ(pipelines.get_cached_pipeline_count(), 4u);
            }
        }
        frames.wait_for_all_slots();
        EXPECT_TRUE(retired.expired());
        pipelines.collect_unused();
        EXPECT_EQ(pipelines.get_cached_pipeline_count(), 2u);
        const auto* all_pixels =
            static_cast<const uint8_t*>(device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        for(int iteration = 0; iteration < 4; ++iteration) {
            const auto* pixels = all_pixels + iteration * 64 * 32 * 4;
            const auto check = [&](uint32_t x, Math::Vec3i expected) {
                for(int channel = 0; channel < 3; ++channel) {
                    EXPECT_NEAR(pixels[(16 * 64 + x) * 4 + channel], expected[channel], 2)
                        << "iteration=" << iteration << " x=" << x << " channel=" << channel;
                }
            };
            if(iteration == 0) {
                check(16, {191, 0, 32});
                check(48, {26, 102, 51});
            } else {
                check(16, {64, 0, 96});
                check(48, {13, 51, 26});
            }
        }
        device.get().unmapMemory(*memory);
        auto rebuilt = MaterialRenderer::create(
            device, pipelines, engine->get_resource_manager(), 2, SampleCount::Count1, &relocated);
        ASSERT_TRUE(rebuilt) << rebuilt.error();
        for(const auto& layout : rebuilt.value()->get_material_layouts())
            EXPECT_EQ(layout->get_parameter_size(), 48u);
    }
}
