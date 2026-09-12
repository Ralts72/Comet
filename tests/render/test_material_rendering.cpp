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
#include "render/resource/mesh.h"
#include "render/resource/texture.h"

#include <gtest/gtest.h>
#include <optional>

namespace Comet::Tests {
    class MaterialRenderingTest: public EngineTest {
    protected:
        std::shared_ptr<Texture> texture(std::vector<uint8_t> rgba) {
            return engine->get_resource_manager()
                .try_create_texture({.width = 1, .height = 1, .pixels = std::move(rgba)})
                .value();
        }
    };

    TEST_F(
        MaterialRenderingTest, ReadsPixelsFromTwoLayoutsBeforeAndAfterParameterChanges) {
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
        const auto textured =
            std::make_shared<Material>("textured", "unlit_texture_blend");
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

        // Test-only host-coherent readback; no production Texture API is needed.
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
                textured->set_texture_property("u_Texture0", texture({255, 0, 0, 255}));
                textured->set_scalar_property("blend", 0.75f);
                solid->set_scalar_property("intensity", 0.25f);
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
            EXPECT_EQ(materials.get_statistics().material_versions_created, 2u);
        }
        // Both slots were submitted; old resources stay owned until slot collection.
        EXPECT_FALSE(retired.expired());
        frames.wait_for_all_slots();
        EXPECT_TRUE(retired.expired());
        const auto* all_pixels = static_cast<const uint8_t*>(
            device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE));
        for(int iteration = 0; iteration < 2; ++iteration) {
            const auto* pixels = all_pixels + iteration * 64 * 32 * 4;
            const auto check = [&](uint32_t x, Math::Vec3i expected) {
                for(int channel = 0; channel < 3; ++channel) {
                    EXPECT_NEAR(pixels[(16 * 64 + x) * 4 + channel], expected[channel], 2)
                        << "iteration=" << iteration << " x=" << x
                        << " channel=" << channel;
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
    }
}
