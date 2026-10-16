#include "render/renderer.h"
#include "render/render_thread.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "render/scene/scene_renderer.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"
#include "render/resource/render_resources.h"
#include "render/resource/texture.h"

#include <algorithm>

namespace Comet {
    Result<TextureData, GraphicsError> Renderer::capture_path_trace(ViewId id) {
        auto captured = capture_path_trace_inputs(id, false);
        if(!captured)
            return Result<TextureData, GraphicsError>::failure(captured.error());
        return Result<TextureData, GraphicsError>::success(std::move(captured).value().color);
    }
    PathTracePass::Statistics Renderer::path_trace_statistics(ViewId id) const {
        return get_view(id).scene->get_path_trace_statistics();
    }
    Result<Renderer::PathTraceCapture, GraphicsError> Renderer::capture_path_trace_inputs(
        ViewId id, bool features) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, id, features] { return capture_path_trace_inputs(id, features); });
        using Captured = Result<PathTraceCapture, GraphicsError>;
        if(m_shutdown_prepared || !m_views.contains(id))
            return Captured::failure({"路径追踪视图不可用"});
        const auto& scene = *get_view(id).scene;
        if(scene.get_path_trace_statistics().samples == 0)
            return Captured::failure({"尚无可导出的路径追踪样本"});
        const auto views = scene.get_path_trace_inputs();
        if(features && (!views[1] || !views[2]))
            return Captured::failure({"请开启降噪并重新采样，以生成辅助特征"});
        const auto size = views[0]->get_image()->get_info().extent;
        const auto statistics = scene.get_path_trace_statistics();
        PathTraceCapture captured;
        captured.generation = statistics.generation;
        captured.samples = statistics.samples;
        auto& device = m_render_context->get_device();
        const auto bytes = std::size_t(size.x) * size.y * sizeof(Math::Vec4);
        const uint32_t image_count = features ? 3 : 1;
        const std::array<TextureData*, 3> pixels{
            &captured.color, &captured.albedo, &captured.normal};
        for(uint32_t i = 0; i < image_count; ++i)
            *pixels[i] = {.width = int(size.x),
                .height = int(size.y),
                .format = Format::R32G32B32A32_SFLOAT,
                .pixels = std::vector<uint8_t>(bytes)};
        auto buffer =
            Buffer::try_create_readback_buffer(device, bytes * image_count, "path trace capture");
        if(!buffer)
            return Captured::failure(buffer.error());
        FrameScheduler frames(device, 1);
        frames.initialize_swapchain_images(1);
        frames.wait_for_current_slot();
        frames.begin_frame(0);
        auto& command = frames.get_current_command_buffer();
        command.begin();
        RenderGraph graph;
        const auto initial = *resolve_image_state(ResourceUsage::SampledRead,
            {.aspects = Flags<ImageAspect>(ImageAspect::Color)},
            Flags<PipelineStage>(PipelineStage::FragmentShader));
        std::vector<RenderGraph::Binding> bindings;
        std::vector<RenderGraph::Use> uses;
        for(uint32_t i = 0; i < image_count; ++i) {
            const auto from = graph.import_image("path trace input " + std::to_string(i), initial);
            bindings.push_back(views[i]->get_image());
            uses.push_back({from, ResourceUsage::TransferSource, {}});
            graph.export_resource({from, ResourceUsage::SampledRead,
                Flags<PipelineStage>(PipelineStage::FragmentShader)});
        }
        const auto to = graph.import_buffer("capture pixels", {{}, 0, buffer.value()->get_size()});
        bindings.push_back(std::static_pointer_cast<Buffer>(buffer.value()));
        uses.push_back({to, ResourceUsage::TransferDestination, {}});
        graph.add_pass({"copy capture", std::move(uses)});
        graph.export_resource({to, ResourceUsage::HostRead, {}});
        auto plan = graph.compile();
        if(!plan)
            return Captured::failure({plan.error()});
        auto recorded = plan.value().record(frames, bindings, [&](auto, CommandBuffer& cmd) {
            vk::BufferImageCopy region;
            region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.imageExtent = vk::Extent3D(size.x, size.y, 1);
            for(uint32_t i = 0; i < image_count; ++i) {
                region.bufferOffset = bytes * i;
                cmd.get().copyImageToBuffer(views[i]->get_image()->get(),
                    vk::ImageLayout::eTransferSrcOptimal, buffer.value()->get(), region);
            }
            return Result<void, GraphicsError>::success();
        });
        if(!recorded)
            return Captured::failure(recorded.error());
        command.end();
        auto submitted = frames.submit({}, {});
        if(!submitted)
            return Captured::failure(submitted.error());
        frames.end_frame();
        frames.wait_for_all_slots();
        std::vector<uint8_t> packed(bytes * image_count);
        if(auto read = buffer.value()->read(std::as_writable_bytes(std::span(packed))); !read)
            return Captured::failure(read.error());
        for(uint32_t i = 0; i < image_count; ++i)
            std::copy_n(packed.begin() + bytes * i, bytes, pixels[i]->pixels.begin());
        return Captured::success(std::move(captured));
    }
    Result<bool, GraphicsError> Renderer::publish_path_trace_denoised(
        TextureData color, uint64_t generation, uint32_t samples, ViewId id) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke(
                [this, color = std::move(color), generation, samples, id]() mutable {
                    return publish_path_trace_denoised(std::move(color), generation, samples, id);
                });
        using Published = Result<bool, GraphicsError>;
        if(m_shutdown_prepared || !m_views.contains(id))
            return Published::success(false);
        auto& scene = *get_view(id).scene;
        const auto current = scene.get_path_trace_statistics();
        if(current.samples == 0 || current.generation != generation || current.samples != samples)
            return Published::success(false);
        if(color.width != int(current.size.x) || color.height != int(current.size.y)
            || color.format != Format::R32G32B32A32_SFLOAT || color.cubemap || color.mip_levels != 1
            || color.pixels.size() != std::size_t(color.width) * color.height * sizeof(Math::Vec4))
            return Published::failure({"降噪结果与当前积分尺寸或格式不一致"});
        auto texture = m_render_resources->try_create_texture(std::move(color));
        if(!texture)
            return Published::failure({"无法上传降噪图像", texture.result()});
        texture.value()->get_ready_completion().wait();
        return Published::success(scene.publish_path_trace_denoised(
            texture.value()->get_image_view(), generation, samples));
    }
}
