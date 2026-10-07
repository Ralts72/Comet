#pragma once

#include "core/engine.h"
#include "config/config.h"
#include "graphics/context.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "graphics/render_pass.h"
#include "graphics/attachment.h"
#include "render/renderer.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "render/render_graph.h"
#include "render/render_target.h"
#include "render/scene/scene_renderer.h"
#include "render/material/material_programs.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <cstring>
#include <cmath>
#include <sstream>

namespace Comet::Tests {
    template<typename T> T encode_srgb(T linear) {
        if(linear <= T(0.0031308))
            return T(12.92) * linear;
        return T(1.055) * std::pow(linear, T(1.0 / 2.4)) - T(0.055);
    }

    class RenderGpuTest: public testing::Test {
    protected:
        // 测试专用 host-coherent 读回 owner，不改变生产 Buffer 的映射策略。
        class Readback final: public Buffer {
        public:
            Readback(Device& device, vk::PhysicalDevice physical, uint64_t size)
                : Buffer(device, size) {
                native = device.get().createBufferUnique(
                    {{}, size, vk::BufferUsageFlagBits::eTransferDst, vk::SharingMode::eExclusive});
                const auto requirements = device.get().getBufferMemoryRequirements(*native);
                const auto properties = physical.getMemoryProperties();
                const auto required = vk::MemoryPropertyFlagBits::eHostVisible
                                      | vk::MemoryPropertyFlagBits::eHostCoherent;
                std::optional<uint32_t> type;
                for(uint32_t index = 0; index < properties.memoryTypeCount; ++index)
                    if((requirements.memoryTypeBits & (1u << index))
                        && (properties.memoryTypes[index].propertyFlags & required) == required) {
                        type = index;
                        break;
                    }
                if(!type)
                    return;
                memory = device.get().allocateMemoryUnique({requirements.size, *type});
                device.get().bindBufferMemory(*native, *memory, 0);
                m_buffer = *native;
            }
            std::vector<std::byte> read() const {
                std::vector<std::byte> result(m_size);
                const auto* data = m_device.get().mapMemory(*memory, 0, VK_WHOLE_SIZE);
                std::memcpy(result.data(), data, result.size());
                m_device.get().unmapMemory(*memory);
                return result;
            }

        private:
            vk::UniqueDeviceMemory memory;
            vk::UniqueBuffer native;
        };
        std::unique_ptr<Engine> engine;
        std::ostringstream messages;
        std::shared_ptr<spdlog::sinks::ostream_sink_mt> sink;
        std::shared_ptr<RenderPass> presentation_pass;
        std::unique_ptr<RenderTarget> presentation_target;
        void SetUp() override {
            sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(messages);
            sink->set_level(spdlog::level::err);
            Logger::add_custom_sink(sink);
            Config config;
            config.window.width = 160;
            config.window.height = 120;
            config.vulkan.enable_validation = true;
            config.vulkan.msaa_samples = SampleCount::Count1;
            auto created = Engine::create(config);
            ASSERT_TRUE(created) << created.error().message;
            engine = std::move(created).value();
        }
        void TearDown() override {
            if(presentation_target) {
                engine->get_renderer().wait_idle();
                engine->get_renderer().set_overlay({});
                presentation_target.reset();
                presentation_pass.reset();
            }
            engine.reset();
            if(auto logger = Logger::get_console_logger())
                std::erase(logger->sinks(), sink);
            EXPECT_EQ(messages.str().find("VUID-"), std::string::npos) << messages.str();
            EXPECT_EQ(messages.str().find("Validation Error"), std::string::npos) << messages.str();
            EXPECT_TRUE(messages.str().empty()) << messages.str();
        }
        Result<std::unique_ptr<SceneRenderer>, GraphicsError> create_scene(
            MaterialPrograms& programs, Math::Vec2u size,
            SampleCount samples = SampleCount::Count1) {
            Config config;
            config.vulkan.msaa_samples = samples;
            return SceneRenderer::create(engine->get_renderer().get_render_context().get_device(),
                programs, engine->get_render_resources(), config.vulkan, config.render, size);
        }
        Result<void, GraphicsError> prepare_offscreen_host(Math::Vec2u size) {
            auto& renderer = engine->get_renderer();
            if(auto enabled = renderer.enable_offscreen_rendering(size); !enabled)
                return enabled;
            auto& context = renderer.get_render_context();
            const auto format = context.get_swapchain().get_images().front()->get_info().format;
            auto pass = RenderPass::create(context.get_device(),
                {Attachment::get_color_attachment(format, SampleCount::Count1)},
                {{.color_attachments = {SubpassColorAttachment(0)}}}, format);
            if(!pass)
                return Result<void, GraphicsError>::failure(pass.error());
            auto target = RenderTarget::create_swapchain_target(
                context.get_device(), *pass.value(), context.get_swapchain());
            if(!target)
                return Result<void, GraphicsError>::failure(target.error());
            presentation_pass = std::move(pass).value();
            presentation_target = std::move(target).value();
            renderer.set_overlay({.render = [this](OverlayRecordContext& overlay) {
                auto& command = overlay.command_buffer();
                presentation_target->begin_render_target(command);
                presentation_target->end_render_target(command);
                return Result<void, GraphicsError>::success();
            }});
            return Result<void, GraphicsError>::success();
        }
        static void submit(
            Device&, FrameScheduler& frames, std::span<const QueueSemaphoreSubmit> waits = {}) {
            frames.get_current_command_buffer().end();
            const auto submitted = frames.submit(waits, {});
            ASSERT_TRUE(submitted) << submitted.error().message;
            frames.end_frame();
        }
        struct FrameWait {
            Device& device;
            FrameScheduler& frames;
            ~FrameWait() {
                if(frames.is_recording_frame())
                    RenderGpuTest::submit(device, frames);
                frames.wait_for_all_slots();
            }
        };
        static void copy_output(FrameScheduler& frames, const std::shared_ptr<Image>& image,
            const std::shared_ptr<Readback>& readback, Math::Vec2u size) {
            RenderGraph graph;
            auto state = *resolve_image_state(ResourceUsage::SampledRead,
                {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
                Flags<PipelineStage>(PipelineStage::FragmentShader));
            // RenderPass 的 final layout 已生效，但最后的数据生产者仍是颜色附件写入。
            state.resource = *resolve_resource_state(ResourceUsage::ColorAttachmentWrite);
            const auto source = graph.import_image("SDR", state);
            const auto destination = graph.import_buffer("host", {{}, 0, readback->get_size()});
            graph.add_pass(
                {"readback", {{source, ResourceUsage::TransferSource, {}},
                                 {destination, ResourceUsage::TransferDestination, {}}}});
            graph.export_resource({destination, ResourceUsage::HostRead, {}});
            const std::vector<RenderGraph::Binding> bindings{
                image, std::static_pointer_cast<Buffer>(readback)};
            auto plan = graph.compile();
            ASSERT_TRUE(plan) << plan.error();
            ASSERT_TRUE(plan.value().record(frames, bindings, [&](size_t, CommandBuffer& command) {
                vk::BufferImageCopy region;
                region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
                region.imageExtent = vk::Extent3D(size.x, size.y, 1);
                command.get().copyImageToBuffer(
                    image->get(), vk::ImageLayout::eTransferSrcOptimal, readback->get(), region);
                return Result<void, GraphicsError>::success();
            }));
        }
        static void render_and_copy(Renderer& renderer, const RenderScene& scene,
            FrameScheduler& readback_frames, const std::shared_ptr<Readback>& readback) {
            auto prepared = renderer.prepare_frame();
            ASSERT_TRUE(prepared) << prepared.error();
            ASSERT_EQ(prepared.value(), Renderer::FramePreparation::Ready);
            const auto output = renderer.get_offscreen_frame();
            ASSERT_TRUE(output.color_view);
            ASSERT_TRUE(renderer.render_frame(scene));
            readback_frames.wait_for_current_slot();
            readback_frames.begin_frame(0);
            readback_frames.get_current_command_buffer().begin();
            copy_output(readback_frames, output.color_view->get_image(), readback, output.size);
            submit(readback_frames.get_device(), readback_frames);
        }
    };
}
