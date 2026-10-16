#include "render/renderer.h"
#include "render/render_thread.h"
#include "render/render_context.h"
#include "render/frame_scheduler.h"
#include "render/scene/scene_renderer.h"
#include "graphics/device.h"
#include "graphics/resource/buffer.h"
#include "graphics/resource/image.h"
#include "graphics/resource/image_view.h"

namespace Comet {
    Result<TextureData, GraphicsError> Renderer::capture_path_trace(ViewId id) {
        if(m_render_thread && !m_render_thread->is_current())
            return m_render_thread->invoke([this, id] { return capture_path_trace(id); });
        using Captured = Result<TextureData, GraphicsError>;
        if(m_shutdown_prepared || !m_views.contains(id))
            return Captured::failure({"路径追踪视图不可用"});
        const auto& scene = *get_view(id).scene;
        if(scene.get_path_trace_statistics().samples == 0)
            return Captured::failure({"尚无可导出的路径追踪样本"});
        const auto image = scene.get_hdr_color_view(0)->get_image();
        const auto size = image->get_info().extent;
        auto& device = m_render_context->get_device();
        TextureData pixels{.width = int(size.x),
            .height = int(size.y),
            .format = Format::R32G32B32A32_SFLOAT,
            .pixels = std::vector<uint8_t>(std::size_t(size.x) * size.y * sizeof(Math::Vec4))};
        auto buffer =
            Buffer::try_create_readback_buffer(device, pixels.pixels.size(), "path trace export");
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
        const auto from = graph.import_image("path trace HDR", initial);
        const auto to = graph.import_buffer("export pixels", {{}, 0, buffer.value()->get_size()});
        graph.add_pass({"copy export", {{from, ResourceUsage::TransferSource, {}},
                                           {to, ResourceUsage::TransferDestination, {}}}});
        graph.export_resource({from, ResourceUsage::SampledRead,
            Flags<PipelineStage>(PipelineStage::FragmentShader)});
        graph.export_resource({to, ResourceUsage::HostRead, {}});
        auto plan = graph.compile();
        if(!plan)
            return Captured::failure({plan.error()});
        const std::array<RenderGraph::Binding, 2> bindings{
            image, std::static_pointer_cast<Buffer>(buffer.value())};
        auto recorded = plan.value().record(frames, bindings, [&](auto, CommandBuffer& cmd) {
            vk::BufferImageCopy region;
            region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
            region.imageExtent = vk::Extent3D(size.x, size.y, 1);
            cmd.get().copyImageToBuffer(
                image->get(), vk::ImageLayout::eTransferSrcOptimal, buffer.value()->get(), region);
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
        if(auto read = buffer.value()->read(std::as_writable_bytes(std::span(pixels.pixels)));
            !read)
            return Captured::failure(read.error());
        return Captured::success(std::move(pixels));
    }
}
